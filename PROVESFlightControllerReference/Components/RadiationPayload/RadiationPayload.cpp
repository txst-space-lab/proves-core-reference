// ======================================================================
// \title  RadiationPayload.cpp
// \brief  RadiationPayload component implementation.
//
// Collects gamma radiation readings from the MOSAIC payload over UART.
// Protocol: MOSAIC sends <GAMMA_START><SIZE>[4-byte LE uint32]</SIZE>[data]
// This component ACKs each chunk with <TXSTATE> and requests the next
// reading with "gamma_begin\n" until powered off.
//
// Each file holds READINGS_PER_FILE readings (default 100). When full,
// the file is closed and a new one is opened automatically.
// ======================================================================

#include "PROVESFlightControllerReference/Components/RadiationPayload/RadiationPayload.hpp"

#include <cinttypes>
#include <cstring>

#include "Fw/Types/Assert.hpp"
#include "Fw/Types/BasicTypes.hpp"
#include "Os/File.hpp"

namespace Components {

// ----------------------------------------------------------------------
// Construction and destruction
// ----------------------------------------------------------------------

RadiationPayload::RadiationPayload(const char* const compName)
    : RadiationPayloadComponentBase(compName) {}

RadiationPayload::~RadiationPayload() {
    closeCurrentFile();
}

// ----------------------------------------------------------------------
// Command handlers
// ----------------------------------------------------------------------

void RadiationPayload::POWER_ON_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    if (m_powered) {
        this->log_WARNING_LO_AlreadyOn();
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
        return;
    }

    if (!openNextFile()) {
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }

    m_powered = true;
    m_readingsInFile = 0;

    this->log_ACTIVITY_HI_PayloadOn();
    this->tlmWrite_PowerState(true);
    this->tlmWrite_ReadingsInCurrentFile(m_readingsInFile);
    this->tlmWrite_FilesWritten(m_filesWritten);
    this->tlmWrite_TotalReadings(m_totalReadings);

    requestReading();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void RadiationPayload::POWER_OFF_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    if (!m_powered) {
        this->log_WARNING_LO_AlreadyOff();
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
        return;
    }

    m_powered = false;
    m_receiving = false;
    m_bytes_received = 0;
    m_expected_size = 0;
    clearProtocolBuffer();
    closeCurrentFile();

    this->log_ACTIVITY_HI_PayloadOff(m_totalReadings);
    this->tlmWrite_PowerState(false);
    this->tlmWrite_ReadingsInCurrentFile(m_readingsInFile);
    this->tlmWrite_FilesWritten(m_filesWritten);
    this->tlmWrite_TotalReadings(m_totalReadings);

    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

// ----------------------------------------------------------------------
// Port handlers
// ----------------------------------------------------------------------

void RadiationPayload::dataIn_handler(FwIndexType portNum,
                                      Fw::Buffer& buffer,
                                      const Drv::ByteStreamStatus& status) {
    if (!m_powered) {
        return;
    }

    if (status != Drv::ByteStreamStatus::OP_OK) {
        if (m_receiving) {
            handleTransferError();
        }
        return;
    }

    if (!buffer.isValid()) {
        return;
    }

    const U8* data = buffer.getData();
    U32 dataSize = static_cast<U32>(buffer.getSize());

    if (m_receiving && m_fileOpen) {
        U32 remaining = m_expected_size - m_bytes_received;
        U32 toWrite = (dataSize < remaining) ? dataSize : remaining;

        if (!writeChunkToFile(data, toWrite)) {
            Fw::LogStringArg desc("File write failed");
            this->log_WARNING_HI_TransferError(desc);
            handleTransferError();
            return;
        }

        m_bytes_received += toWrite;
        sendAck();

        if (m_bytes_received >= m_expected_size) {
            finalizeReading();

            // If MOSAIC sent extra bytes (next header), process them now
            U32 extraBytes = dataSize - toWrite;
            if (extraBytes > 0 && m_powered) {
                if (accumulateProtocolData(data + toWrite, extraBytes)) {
                    processProtocolBuffer();
                }
            }
        }
    } else {
        // Not mid-transfer — look for incoming header
        if (m_protocolBufferSize > (PROTOCOL_BUFFER_SIZE * 9 / 10)) {
            if (m_protocolBufferSize > 32) {
                memmove(m_protocolBuffer, &m_protocolBuffer[m_protocolBufferSize - 32], 32);
                m_protocolBufferSize = 32;
            }
        }

        if (!accumulateProtocolData(data, dataSize)) {
            clearProtocolBuffer();
            accumulateProtocolData(data, dataSize);
        }

        processProtocolBuffer();
    }
}

void RadiationPayload::run_handler(FwIndexType portNum, U32 context) {
    this->tlmWrite_PowerState(m_powered);
    this->tlmWrite_ReadingsInCurrentFile(m_readingsInFile);
    this->tlmWrite_FilesWritten(m_filesWritten);
    this->tlmWrite_TotalReadings(m_totalReadings);
}

// ----------------------------------------------------------------------
// Protocol helpers (adapted from MosaicHandler)
// ----------------------------------------------------------------------

bool RadiationPayload::accumulateProtocolData(const U8* data, U32 size) {
    if (m_protocolBufferSize + size > PROTOCOL_BUFFER_SIZE) {
        return false;
    }
    memcpy(&m_protocolBuffer[m_protocolBufferSize], data, size);
    m_protocolBufferSize += size;
    return true;
}

void RadiationPayload::processProtocolBuffer() {
    // Search for <GAMMA_START> in the accumulated buffer
    I32 headerStart = -1;
    if (m_protocolBufferSize >= GAMMA_START_LEN) {
        for (U32 i = 0; i <= m_protocolBufferSize - GAMMA_START_LEN; ++i) {
            if (isGammaStartCommand(&m_protocolBuffer[i], m_protocolBufferSize - i)) {
                headerStart = static_cast<I32>(i);
                break;
            }
        }
    }

    if (headerStart == -1) {
        // No header yet — trim buffer if it's growing large
        if (m_protocolBufferSize > (PROTOCOL_BUFFER_SIZE / 2)) {
            if (m_protocolBufferSize > 16) {
                memmove(m_protocolBuffer, &m_protocolBuffer[m_protocolBufferSize - 16], 16);
                m_protocolBufferSize = 16;
            } else {
                clearProtocolBuffer();
            }
        }
        return;
    }

    // Discard bytes before the header
    if (headerStart > 0) {
        U32 remaining = m_protocolBufferSize - static_cast<U32>(headerStart);
        memmove(m_protocolBuffer, &m_protocolBuffer[headerStart], remaining);
        m_protocolBufferSize = remaining;
    }

    // Wait until we have the full header
    if (m_protocolBufferSize < HEADER_SIZE) {
        return;
    }

    // Verify <SIZE> tag
    const char* sizeTag = "<SIZE>";
    for (U32 i = 0; i < SIZE_TAG_LEN; ++i) {
        if (m_protocolBuffer[SIZE_TAG_OFFSET + i] != static_cast<U8>(sizeTag[i])) {
            return;
        }
    }

    // Extract 4-byte little-endian size
    U32 readingSize = 0;
    readingSize |= static_cast<U32>(m_protocolBuffer[SIZE_VALUE_OFFSET + 0]);
    readingSize |= static_cast<U32>(m_protocolBuffer[SIZE_VALUE_OFFSET + 1]) << 8;
    readingSize |= static_cast<U32>(m_protocolBuffer[SIZE_VALUE_OFFSET + 2]) << 16;
    readingSize |= static_cast<U32>(m_protocolBuffer[SIZE_VALUE_OFFSET + 3]) << 24;

    // Verify </SIZE> tag
    const char* closeSizeTag = "</SIZE>";
    for (U32 i = 0; i < SIZE_CLOSE_TAG_LEN; ++i) {
        if (m_protocolBuffer[SIZE_CLOSE_TAG_OFFSET + i] != static_cast<U8>(closeSizeTag[i])) {
            return;
        }
    }

    // Valid header — begin transfer (file is already open from POWER_ON / rotation)
    m_receiving = true;
    m_bytes_received = 0;
    m_expected_size = readingSize;

    sendAck();

    // Remove the header from the buffer
    U32 afterHeader = m_protocolBufferSize - HEADER_SIZE;
    if (afterHeader > 0) {
        memmove(m_protocolBuffer, &m_protocolBuffer[HEADER_SIZE], afterHeader);
    }
    m_protocolBufferSize = afterHeader;

    // Write any payload bytes that immediately followed the header
    if (m_protocolBufferSize > 0 && m_fileOpen) {
        U32 toWrite = (m_protocolBufferSize < m_expected_size) ? m_protocolBufferSize : m_expected_size;
        if (writeChunkToFile(m_protocolBuffer, toWrite)) {
            m_bytes_received += toWrite;
            if (m_bytes_received >= m_expected_size) {
                finalizeReading();
            }
        } else {
            handleTransferError();
        }
        clearProtocolBuffer();
    }
}

void RadiationPayload::clearProtocolBuffer() {
    m_protocolBufferSize = 0;
    memset(m_protocolBuffer, 0, PROTOCOL_BUFFER_SIZE);
}

bool RadiationPayload::writeChunkToFile(const U8* data, U32 size) {
    if (!m_fileOpen || size == 0) {
        return false;
    }

    U32 totalWritten = 0;
    const U8* ptr = data;
    while (totalWritten < size) {
        FwSizeType toWrite = static_cast<FwSizeType>(size - totalWritten);
        Os::File::Status status = m_file.write(ptr, toWrite, Os::File::WaitType::WAIT);
        if (status != Os::File::OP_OK) {
            return false;
        }
        totalWritten += static_cast<U32>(toWrite);
        ptr += toWrite;
    }
    return true;
}

void RadiationPayload::finalizeReading() {
    // Delimit readings with a newline
    const U8 newline = '\n';
    FwSizeType nlSize = 1;
    m_file.write(&newline, nlSize, Os::File::WaitType::WAIT);

    m_readingsInFile++;
    m_totalReadings++;

    Fw::LogStringArg pathArg(m_currentFilename);
    this->log_ACTIVITY_HI_GammaReadingReceived(m_bytes_received, pathArg);
    this->log_ACTIVITY_LO_ReadingComplete(m_readingsInFile, m_filesWritten);

    m_receiving = false;
    m_bytes_received = 0;
    m_expected_size = 0;

    sendAck();

    // Rotate file if the reading limit has been reached
    Fw::ParamValid valid;
    U32 readingsPerFile = this->paramGet_READINGS_PER_FILE(valid);
    if (valid != Fw::ParamValid::VALID) {
        readingsPerFile = 100;
    }

    if (m_readingsInFile >= readingsPerFile) {
        this->log_ACTIVITY_LO_FileComplete(m_filesWritten, m_readingsInFile);
        closeCurrentFile();
        m_filesWritten++;
        m_readingsInFile = 0;

        if (m_powered) {
            if (!openNextFile()) {
                m_powered = false;
                this->tlmWrite_PowerState(false);
                return;
            }
        }
    }

    this->tlmWrite_ReadingsInCurrentFile(m_readingsInFile);
    this->tlmWrite_FilesWritten(m_filesWritten);
    this->tlmWrite_TotalReadings(m_totalReadings);

    if (m_powered) {
        requestReading();
    }
}

void RadiationPayload::handleTransferError() {
    Fw::LogStringArg desc("Transfer aborted");
    this->log_WARNING_HI_TransferError(desc);
    m_receiving = false;
    m_bytes_received = 0;
    m_expected_size = 0;
    clearProtocolBuffer();
    // Keep the file open — the next successful reading will continue appending
}

bool RadiationPayload::isGammaStartCommand(const U8* data, U32 length) {
    if (length < GAMMA_START_LEN) {
        return false;
    }
    const char* marker = "<GAMMA_START>";
    for (U32 i = 0; i < GAMMA_START_LEN; ++i) {
        if (data[i] != static_cast<U8>(marker[i])) {
            return false;
        }
    }
    return true;
}

void RadiationPayload::sendAck() {
    const char* ackMsg = "<TXSTATE>\n";
    Fw::Buffer ackBuffer(reinterpret_cast<U8*>(const_cast<char*>(ackMsg)), strlen(ackMsg));
    this->commandOut_out(0, ackBuffer, Drv::ByteStreamStatus::OP_OK);
}

void RadiationPayload::requestReading() {
    const char* cmd = "gamma_begin\n";
    Fw::Buffer cmdBuffer(reinterpret_cast<U8*>(const_cast<char*>(cmd)), strlen(cmd));
    this->commandOut_out(0, cmdBuffer, Drv::ByteStreamStatus::OP_OK);
}

// ----------------------------------------------------------------------
// File management helpers
// ----------------------------------------------------------------------

void RadiationPayload::configure() {
    // Pre-load the persisted file count so POWER_ON needs no disk read.
    // Called from the topology after fsFormat.configure(), guaranteeing
    // the filesystem is ready before we touch it.
    U32 count = 0;
    readFileCount(count);
    m_nextFileCount = count;
}

bool RadiationPayload::openNextFile() {
    snprintf(m_currentFilename, sizeof(m_currentFilename), "/rad_%03" PRIu32 ".txt", m_nextFileCount);

    Os::File::Status status =
        m_file.open(m_currentFilename, Os::File::OPEN_CREATE, Os::File::OVERWRITE);
    if (status != Os::File::OP_OK) {
        Fw::LogStringArg desc("Failed to open file");
        this->log_WARNING_HI_FileError(desc);
        return false;
    }

    m_nextFileCount++;
    writeFileCount(m_nextFileCount);
    m_fileOpen = true;
    return true;
}

void RadiationPayload::closeCurrentFile() {
    if (m_fileOpen) {
        m_file.close();
        m_fileOpen = false;
    }
}

bool RadiationPayload::readFileCount(U32& count) {
    Os::File file;
    U8 buffer[sizeof(U32)];
    Fw::ExternalSerializeBuffer deserializer(buffer, sizeof(buffer));

    Os::File::Status status = file.open(FILE_COUNT_PATH, Os::File::OPEN_READ);
    if (status != Os::File::OP_OK) {
        file.close();
        return false;
    }

    FwSizeType size = sizeof(buffer);
    status = file.read(buffer, size);
    file.close();

    if (status != Os::File::OP_OK || size != sizeof(buffer)) {
        return false;
    }

    deserializer.setBuffLen(size);
    return deserializer.deserializeTo(count) == Fw::SerializeStatus::FW_SERIALIZE_OK;
}

bool RadiationPayload::writeFileCount(U32 count) {
    Os::File file;
    U8 buffer[sizeof(U32)];
    Fw::ExternalSerializeBuffer serializer(buffer, sizeof(buffer));

    Fw::SerializeStatus serStatus = serializer.serializeFrom(count);
    FW_ASSERT(serStatus == Fw::SerializeStatus::FW_SERIALIZE_OK);

    Os::File::Status status =
        file.open(FILE_COUNT_PATH, Os::File::OPEN_CREATE, Os::File::OVERWRITE);
    if (status != Os::File::OP_OK) {
        file.close();
        return false;
    }

    FwSizeType size = sizeof(buffer);
    status = file.write(buffer, size);
    file.close();
    return (status == Os::File::OP_OK && size == sizeof(buffer));
}

}  // namespace Components
