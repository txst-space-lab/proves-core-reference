// ======================================================================
// \title  RadiationPayload.hpp
// \brief  Header for RadiationPayload component.
//         Collects MOSAIC gamma radiation readings and stores them on disk.
//         Uses the <GAMMA_START><SIZE>[4-byte LE uint32]</SIZE>[data] protocol.
// ======================================================================

#ifndef Components_RadiationPayload_HPP
#define Components_RadiationPayload_HPP

#include "Os/File.hpp"
#include "PROVESFlightControllerReference/Components/RadiationPayload/RadiationPayloadComponentAc.hpp"

namespace Components {

class RadiationPayload final : public RadiationPayloadComponentBase {
  public:
    explicit RadiationPayload(const char* const compName);
    ~RadiationPayload();

  private:
    // Port handlers
    void dataIn_handler(FwIndexType portNum,
                        Fw::Buffer& buffer,
                        const Drv::ByteStreamStatus& status) override;

    // Protocol helpers (adapted from MosaicHandler)
    bool accumulateProtocolData(const U8* data, U32 size);
    void processProtocolBuffer();
    void clearProtocolBuffer();
    bool writeChunkToFile(const U8* data, U32 size);
    void finalizeReading();
    void handleTransferError();
    bool isGammaStartCommand(const U8* data, U32 length);
    void sendAck();
    void requestReading();

    // File management helpers
    bool openNextFile();
    void closeCurrentFile();
    bool readFileCount(U32& count);
    bool writeFileCount(U32 count);

  public:
    // Called from topology after filesystem is initialized to pre-load the file count
    void configure();

  private:
    // ON/OFF and file rotation state
    U32 m_readingsInFile = 0;
    U32 m_filesWritten = 0;
    U32 m_totalReadings = 0;
    U32 m_nextFileCount = 0;  // Cached file counter, loaded in configure()

    // Per-transfer state (mirrors MosaicHandler)
    bool m_receiving = false;
    U32 m_bytes_received = 0;
    U32 m_expected_size = 0;

    Os::File m_file;
    bool m_fileOpen = false;
    char m_currentFilename[64] = {};

    // Protocol buffer for header accumulation
    static constexpr U32 PROTOCOL_BUFFER_SIZE = 256;
    U8 m_protocolBuffer[PROTOCOL_BUFFER_SIZE];
    U32 m_protocolBufferSize = 0;

    // Protocol framing constants: <GAMMA_START><SIZE>[4-byte LE uint32]</SIZE>[data]
    static constexpr U32 GAMMA_START_LEN = 13;     // strlen("<GAMMA_START>")
    static constexpr U32 SIZE_TAG_LEN = 6;          // strlen("<SIZE>")
    static constexpr U32 SIZE_VALUE_LEN = 4;        // 4-byte little-endian uint32
    static constexpr U32 SIZE_CLOSE_TAG_LEN = 7;    // strlen("</SIZE>")
    static constexpr U32 HEADER_SIZE =
        GAMMA_START_LEN + SIZE_TAG_LEN + SIZE_VALUE_LEN + SIZE_CLOSE_TAG_LEN;  // 30 bytes
    static constexpr U32 SIZE_TAG_OFFSET = GAMMA_START_LEN;                    // 13
    static constexpr U32 SIZE_VALUE_OFFSET = GAMMA_START_LEN + SIZE_TAG_LEN;   // 19
    static constexpr U32 SIZE_CLOSE_TAG_OFFSET = SIZE_VALUE_OFFSET + SIZE_VALUE_LEN;  // 23

    static constexpr const char* FILE_COUNT_PATH = "/rad_file_count.bin";
};

}  // namespace Components

#endif
