// ======================================================================
// \title  RadiationPayload.cpp
// \brief  RadiationPayload component implementation.
//
// Collects gamma radiation readings from the MOSAIC payload over UART.
// MOSAIC firmware emits ASCII records of the form:
//   <GAMMA_START><ANALOGSIZE>N</ANALOGSIZE>V<MILLIVOLTSIZE>N</MILLIVOLTSIZE>V<GAMMA_END>\n
// We treat each <GAMMA_START>...<GAMMA_END> span as one reading and persist
// it verbatim. READINGS_PER_FILE readings per file before rotating.
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
// Port handlers
// ----------------------------------------------------------------------

void RadiationPayload::dataIn_handler(FwIndexType portNum,
                                      Fw::Buffer& buffer,
                                      const Drv::ByteStreamStatus& status) {
    if (status != Drv::ByteStreamStatus::OP_OK) {
        return;
    }
    if (!buffer.isValid()) {
        return;
    }

    const U8* data = buffer.getData();
    U32 dataSize = static_cast<U32>(buffer.getSize());
    if (dataSize == 0) {
        return;
    }

    m_totalBytesReceived += dataSize;
    m_chunksReceived++;
    this->tlmWrite_BytesReceivedTotal(m_totalBytesReceived);
    this->tlmWrite_DataChunksReceived(m_chunksReceived);

    // Diagnostic: dump the first few chunks so we can see what MOSAIC is actually sending.
    if (m_chunksReceived <= 5 && dataSize >= 8) {
        this->log_ACTIVITY_LO_RawDataDump(data[0], data[1], data[2], data[3],
                                          data[4], data[5], data[6], data[7]);
    }

    if (!accumulateProtocolData(data, dataSize)) {
        // Overflow: drop the buffered partial record and start fresh with this chunk.
        clearProtocolBuffer();
        (void)accumulateProtocolData(data, dataSize);
    }

    processRecords();
}

// ----------------------------------------------------------------------
// Protocol helpers
// ----------------------------------------------------------------------

bool RadiationPayload::accumulateProtocolData(const U8* data, U32 size) {
    if (m_protocolBufferSize + size > PROTOCOL_BUFFER_SIZE) {
        return false;
    }
    memcpy(&m_protocolBuffer[m_protocolBufferSize], data, size);
    m_protocolBufferSize += size;
    return true;
}

void RadiationPayload::processRecords() {
    while (true) {
        I32 startIdx = findMarker(reinterpret_cast<const U8*>("<GAMMA_START>"), GAMMA_START_LEN);
        if (startIdx < 0) {
            // No start marker yet. Trim to keep the buffer from filling with junk,
            // but retain enough trailing bytes in case the marker is split across chunks.
            if (m_protocolBufferSize > (PROTOCOL_BUFFER_SIZE / 2)) {
                U32 keep = (m_protocolBufferSize > GAMMA_START_LEN) ? GAMMA_START_LEN : m_protocolBufferSize;
                memmove(m_protocolBuffer, &m_protocolBuffer[m_protocolBufferSize - keep], keep);
                m_protocolBufferSize = keep;
            }
            return;
        }

        // Discard anything before <GAMMA_START>
        if (startIdx > 0) {
            U32 remaining = m_protocolBufferSize - static_cast<U32>(startIdx);
            memmove(m_protocolBuffer, &m_protocolBuffer[startIdx], remaining);
            m_protocolBufferSize = remaining;
        }

        // Look for <GAMMA_END> after the start marker
        I32 endIdx = findMarker(reinterpret_cast<const U8*>("<GAMMA_END>"), GAMMA_END_LEN, GAMMA_START_LEN);
        if (endIdx < 0) {
            // Incomplete record. If the buffer is already full with no end in sight,
            // the record is malformed/too long — drop it and resync.
            if (m_protocolBufferSize == PROTOCOL_BUFFER_SIZE) {
                clearProtocolBuffer();
            }
            return;
        }

        U32 recordSize = static_cast<U32>(endIdx) + GAMMA_END_LEN;
        writeCompletedRecord(recordSize);

        // Drop the consumed record (and any trailing '\n' MOSAIC appends after <GAMMA_END>)
        U32 consume = recordSize;
        if (consume < m_protocolBufferSize && m_protocolBuffer[consume] == '\n') {
            consume++;
        }
        U32 remaining = m_protocolBufferSize - consume;
        if (remaining > 0) {
            memmove(m_protocolBuffer, &m_protocolBuffer[consume], remaining);
        }
        m_protocolBufferSize = remaining;
    }
}

void RadiationPayload::clearProtocolBuffer() {
    m_protocolBufferSize = 0;
}

I32 RadiationPayload::findMarker(const U8* needle, U32 needleLen, U32 searchFrom) const {
    if (needleLen == 0 || m_protocolBufferSize < searchFrom + needleLen) {
        return -1;
    }
    for (U32 i = searchFrom; i + needleLen <= m_protocolBufferSize; ++i) {
        if (memcmp(&m_protocolBuffer[i], needle, needleLen) == 0) {
            return static_cast<I32>(i);
        }
    }
    return -1;
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

void RadiationPayload::writeCompletedRecord(U32 recordSize) {
    if (!m_fileOpen) {
        // Lazy-open in case configure() couldn't (e.g. FS not ready then).
        if (!openNextFile()) {
            Fw::LogStringArg desc("File open failed during write");
            this->log_WARNING_HI_TransferError(desc);
            return;
        }
    }

    if (!writeChunkToFile(m_protocolBuffer, recordSize)) {
        Fw::LogStringArg desc("File write failed");
        this->log_WARNING_HI_TransferError(desc);
        return;
    }

    const U8 newline = '\n';
    FwSizeType nlSize = 1;
    (void)m_file.write(&newline, nlSize, Os::File::WaitType::WAIT);

    m_readingsInFile++;
    m_totalReadings++;

    Fw::LogStringArg pathArg(m_currentFilename);
    this->log_ACTIVITY_HI_GammaReadingReceived(recordSize, pathArg);

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
        (void)openNextFile();
    }

    this->tlmWrite_ReadingsInCurrentFile(m_readingsInFile);
    this->tlmWrite_FilesWritten(m_filesWritten);
    this->tlmWrite_TotalReadings(m_totalReadings);
}

// ----------------------------------------------------------------------
// File management helpers
// ----------------------------------------------------------------------

void RadiationPayload::configure() {
    // Pre-load the persisted file count and open the first file so that the
    // very first incoming record has somewhere to land. Called from the
    // topology after fsFormat.configure() so the filesystem is ready.
    U32 count = 0;
    readFileCount(count);
    m_nextFileCount = count;
    (void)openNextFile();
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
