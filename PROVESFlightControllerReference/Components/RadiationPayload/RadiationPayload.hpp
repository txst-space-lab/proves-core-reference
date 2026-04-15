// ======================================================================
// \title  RadiationPayload.hpp
// \brief  hpp for RadiationPayload component implementation
// ======================================================================

#ifndef Components_RadiationPayload_HPP
#define Components_RadiationPayload_HPP

#include "PROVESFlightControllerReference/Components/RadiationPayload/RadiationPayloadComponentAc.hpp"
#include <atomic>
#include <vector>
#include "Os/File.hpp"
#include "Os/FileSystem.hpp"
//#include <zephyr/kernel.h>
#include "Fw/Types/BasicTypes.hpp"
///#include "Fw/ExternalSerializeBuffer.hpp"

namespace Components {

class RadiationPayload : public RadiationPayloadComponentBase {
  public:
    RadiationPayload(const char* const compName);
    ~RadiationPayload();

  private:
    void run_handler(FwIndexType portNum, U32 context) override;

    void START_PAYLOAD_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void STOP_PAYLOAD_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void TAKE_GAMMA_READING_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;

    // Helping functions
    void writeBufferToFile();
    bool serializeReadings(Fw::ExternalSerializeBuffer& buffer);

    std::atomic_bool m_run{false}; // Control flag for run loop
    U32 m_fileCounter = 0;
    std::vector<F64> m_buffer; // Buffer to hold readings until we write to file
    U32 m_readingsPerFile = 100;
    static constexpr const char* RADIATION_DIR = "//radiation"; // Dir to save radiation reading files
};

}  // namespace Components

#endif