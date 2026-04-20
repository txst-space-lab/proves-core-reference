// ======================================================================
// \title  RadiationPayload.cpp
// ======================================================================

#include "PROVESFlightControllerReference/Components/RadiationPayload/RadiationPayload.hpp"
#include "Fw/Types/Assert.hpp"
#include <cstring>

namespace Components {

// ----------------------------------------------------------------------
// Construction and destruction
// ----------------------------------------------------------------------

RadiationPayload::RadiationPayload(const char* const compName)
    : RadiationPayloadComponentBase(compName) {}

RadiationPayload::~RadiationPayload() {}

// ----------------------------------------------------------------------
// configure() — called from configureComponents() once the parameter
// database is fully initialised.  Must NOT touch the filesystem.
// ----------------------------------------------------------------------

void RadiationPayload::configure() {
    Fw::ParamValid valid;
    const U32 readings = this->paramGet_READINGS_PER_FILE(valid);
    if (valid == Fw::ParamValid::VALID || valid == Fw::ParamValid::DEFAULT) {
        m_readingsPerFile = readings;
    }
}

// ----------------------------------------------------------------------
// Port handlers
// ----------------------------------------------------------------------

void RadiationPayload::run_handler(FwIndexType portNum, U32 context) {
    (void)portNum;
    (void)context;
    if (m_run && m_bufferCount >= m_readingsPerFile) {
        this->flushBuffer();
    }
}

void RadiationPayload::dataIn_handler(FwIndexType portNum,
                                      Fw::Buffer& buffer,
                                      const Drv::ByteStreamStatus& status) {
    (void)portNum;
    if (!m_run || status != Drv::ByteStreamStatus::OP_OK || buffer.getSize() == 0) {
        return;
    }

    const U8* const data = buffer.getData();
    const FwSizeType numReadings = buffer.getSize() / sizeof(F64);

    for (FwSizeType i = 0; i < numReadings; ++i) {
        if (m_bufferCount >= MAX_READINGS) {
            break;
        }
        F64 reading = 0.0;
        std::memcpy(&reading, data + i * sizeof(F64), sizeof(F64));
        m_buffer[m_bufferCount++] = reading;
        this->tlmWrite_GammaRadiationReading(reading);
    }

    this->tlmWrite_ReadingsCollected(static_cast<FwSizeType>(m_bufferCount));
}

// ----------------------------------------------------------------------
// Command handlers
// ----------------------------------------------------------------------

void RadiationPayload::START_PAYLOAD_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    m_run = true;
    this->log_ACTIVITY_HI_PayloadStarted();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void RadiationPayload::STOP_PAYLOAD_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    m_run = false;
    if (m_bufferCount > 0) {
        this->flushBuffer();
    }
    this->log_ACTIVITY_HI_PayloadStopped();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void RadiationPayload::TAKE_GAMMA_READING_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    // Flush whatever readings are currently buffered into a data product.
    // In full MOSAIC integration, readings accumulate via dataIn_handler;
    // this command forces an immediate flush without waiting for the batch to fill.
    if (m_bufferCount > 0) {
        this->flushBuffer();
    }
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

// ----------------------------------------------------------------------
// Private helpers
// ----------------------------------------------------------------------

void RadiationPayload::flushBuffer() {
    if (m_bufferCount == 0) {
        return;
    }

    // Allocate a data product buffer large enough for all buffered readings.
    // Each GammaReading record serialises as: record-type ID (FwDpIdType) + F64.
    const FwSizeType dataSize =
        static_cast<FwSizeType>(m_bufferCount) * BYTES_PER_RECORD;

    DpContainer container;
    const Fw::Success allocStatus = this->dpGet_RadiationData(dataSize, container);
    if (allocStatus != Fw::Success::SUCCESS) {
        // Retain readings; try again next cycle
        this->log_WARNING_HI_DataProductError();
        return;
    }

    for (U32 i = 0; i < m_bufferCount; ++i) {
        const F64 reading = m_buffer[i];
        const Fw::SerializeStatus sstat =
            container.serializeRecord_GammaReading(reading);
        if (sstat == Fw::FW_SERIALIZE_NO_ROOM_LEFT) {
            break;
        }
        FW_ASSERT(sstat == Fw::FW_SERIALIZE_OK, static_cast<FwAssertArgType>(sstat));
    }

    this->dpSend(container);

    m_bufferCount = 0;
    ++m_dpSentCount;
    this->tlmWrite_DataProductsSent(m_dpSentCount);
    this->log_ACTIVITY_LO_DataProductSent(static_cast<U32>(m_dpSentCount));
}

}  // namespace Components
