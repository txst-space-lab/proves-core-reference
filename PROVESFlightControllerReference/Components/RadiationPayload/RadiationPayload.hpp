// ======================================================================
// \title  RadiationPayload.hpp
// \brief  hpp for RadiationPayload component implementation
// ======================================================================

#ifndef Components_RadiationPayload_HPP
#define Components_RadiationPayload_HPP

#include "PROVESFlightControllerReference/Components/RadiationPayload/RadiationPayloadComponentAc.hpp"
#include "Fw/Types/BasicTypes.hpp"

namespace Components {

class RadiationPayload : public RadiationPayloadComponentBase {
  public:
    RadiationPayload(const char* const compName);
    ~RadiationPayload();

    // Called from configureComponents() after the parameter database is ready
    void configure();

  private:
    // ----------------------------------------------------------------------
    // Handler implementations
    // ----------------------------------------------------------------------

    void run_handler(FwIndexType portNum, U32 context) override;

    void dataIn_handler(FwIndexType portNum,
                        Fw::Buffer& buffer,
                        const Drv::ByteStreamStatus& status) override;

    void START_PAYLOAD_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void STOP_PAYLOAD_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void TAKE_GAMMA_READING_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;

  private:
    // ----------------------------------------------------------------------
    // Helper
    // ----------------------------------------------------------------------

    // Serialize all buffered readings into a data product and send to catalog.
    // Clears m_buffer on success; retains readings on allocation failure.
    void flushBuffer();

  private:
    // ----------------------------------------------------------------------
    // Member variables
    // ----------------------------------------------------------------------

    bool m_run = false;
    FwSizeType m_dpSentCount = 0;
    U32 m_readingsPerFile = 100;

    static constexpr U32 MAX_READINGS = 100;
    F64 m_buffer[MAX_READINGS];
    U32 m_bufferCount = 0;

    // Bytes needed per GammaReading record: record-type ID + F64 value
    static constexpr FwSizeType BYTES_PER_RECORD = sizeof(FwDpIdType) + sizeof(F64);
};

}  // namespace Components

#endif
