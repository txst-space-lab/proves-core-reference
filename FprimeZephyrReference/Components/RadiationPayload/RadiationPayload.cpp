// ======================================================================
// \title  RadiationPayload.cpp
// ======================================================================

#include "FprimeZephyrReference/Components/RadiationPayload/RadiationPayload.hpp"

#include "Fw/Types/BasicTypes.hpp"
#include "Fw/Types/Assert.hpp"
#include <Fw/Time/Time.hpp>
#include <cerrno>
#include <cstdio>

namespace Components {

// ----------------------------------------------------------------------
// Component construction and destruction
// ----------------------------------------------------------------------

RadiationPayload::RadiationPayload(const char* const compName) : RadiationPayloadComponentBase(compName) {
    // Ensure directory exists
    Os::FileSystem::Status dirStatus = Os::FileSystem::createDirectory(RADIATION_DIR, false);
    if (dirStatus != Os::FileSystem::OP_OK) {
        Fw::LogStringArg path(RADIATION_DIR);
        this->log_WARNING_HI_FileOperationError(path, "create_directory");
    }

    // Initialize parameter-driven values
    Fw::ParamValid valid;
    const U32 readings = this->paramGet_READINGS_PER_FILE(valid);
    if (valid == Fw::ParamValid::VALID || valid == Fw::ParamValid::DEFAULT) {
        this->m_readingsPerFile = readings;
    }
}

RadiationPayload::~RadiationPayload() {}

// ----------------------------------------------------------------------
// Handlers
// ----------------------------------------------------------------------

void RadiationPayload::run_handler(FwIndexType portNum, U32 context) {
    (void)portNum;
    (void)context;

    if (!this->m_run) {
        return;
    }

    // Take a reading. In a full integration this should request data from the MOSAIC stack
    // For now use uptime as a deterministic placeholder value
    const double reading = static_cast<double>(k_uptime_seconds());
    this->m_buffer.push_back(reading);
    this->tlmWrite_ReadingsCollected(static_cast<FwSizeType>(this->m_buffer.size()));
    this->tlmWrite_GammaRadiationReading(reading);

    if (this->m_buffer.size() >= this->m_readingsPerFile) {
        this->writeBufferToFile();
    }
}

void RadiationPayload::START_PAYLOAD_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->m_run = true;
    this->log_ACTIVITY_HI_PayloadStarted();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void RadiationPayload::STOP_PAYLOAD_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->m_run = false;
    this->log_ACTIVITY_HI_PayloadStopped();
    // If there is buffered data, attempt to write it
    if (!this->m_buffer.empty()) {
        this->writeBufferToFile();
    }
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void RadiationPayload::TAKE_GAMMA_READING_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    // Immediate one-off reading
    const double reading = static_cast<double>(k_uptime_seconds());
    this->m_buffer.push_back(reading);
    this->tlmWrite_ReadingsCollected(static_cast<FwSizeType>(this->m_buffer.size())); // Update telemetry count
    this->tlmWrite_GammaRadiationReading(reading);

    if (this->m_buffer.size() >= this->m_readingsPerFile) {
        this->writeBufferToFile();
    }

    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

// ----------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------

void RadiationPayload::writeBufferToFile() {
    // Compose filename with file counter and uptime seconds for uniqueness
    char filename[128]; // Local buffer for filename construction
    const uint32_t secs = static_cast<uint32_t>(k_uptime_seconds());
    std::snprintf(filename, sizeof(filename), "%s/readings_%05u_%u.bin", RADIATION_DIR, this->m_fileCounter++, secs);

    Os::File file;
    Os::File::Status status = file.open(filename, Os::File::OPEN_CREATE, Os::File::OVERWRITE);
    if (status != Os::File::OP_OK) {
        Fw::LogStringArg path(filename);
        this->log_WARNING_HI_FileOperationError(path, "open_create");
        return;
    }

    // Serialize readings using F Prime ExternalSerializeBuffer then write to file
    const FwSizeType count = static_cast<FwSizeType>(this->m_buffer.size()); // Number of readings to serialize
    const size_t estimated_size = static_cast<size_t>(Fw::Time::SERIALIZED_SIZE) + sizeof(count) + sizeof(F64) * this->m_buffer.size(); // Estimate size needed for serialization (timestamp + count + readings)
    U8* data = new U8[estimated_size]; // Allocate buffer for serialization
    // Zero buffer to avoid uninitialized bytes
    for (size_t i = 0; i < estimated_size; ++i) {
        data[i] = 0;
    }

    Fw::ExternalSerializeBuffer serializer(data, static_cast<Fw::SerializeBufferBase::BufferSizeType>(estimated_size));
    // Serialize timestamp first
    Fw::Time ts = this->getTime();
    Fw::SerializeStatus sstat = serializer.serializeFrom(ts);
    FW_ASSERT(sstat == Fw::SerializeStatus::FW_SERIALIZE_OK, static_cast<FwAssertArgType>(sstat));

    // Serialize count and readings
    sstat = serializer.serializeFrom(count);
    FW_ASSERT(sstat == Fw::SerializeStatus::FW_SERIALIZE_OK, static_cast<FwAssertArgType>(sstat));

    for (FwSizeType i = 0; i < count; ++i) {
        sstat = serializer.serializeFrom(this->m_buffer[i]);
        FW_ASSERT(sstat == Fw::SerializeStatus::FW_SERIALIZE_OK, static_cast<FwAssertArgType>(sstat));
    }

    // Write the full allocated buffer to disk. Receiver should deserialize using the same format.
    Os::File::Status write_status = file.write(data, static_cast<FwSizeType>(estimated_size));
    (void)file.close();
    delete[] data;

    if (write_status != Os::File::OP_OK) {
        Fw::LogStringArg path(filename);
        this->log_WARNING_HI_FileOperationError(path, "write");
        return;
    }

    Fw::LogStringArg pathArg(filename);
    this->log_ACTIVITY_LO_FileSaved(pathArg);
    this->tlmWrite_FilesWritten(static_cast<FwSizeType>(this->m_fileCounter));
    this->m_buffer.clear();
}

}  // namespace Components
