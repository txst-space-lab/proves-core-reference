module Components {
    @ Passive component that reads radiation data from the MOSAIC payload and stores it on disk.
    @ MOSAIC sends framed readings using <GAMMA_START><SIZE>[4-byte LE uint32]</SIZE>[data] protocol.
    @ Collects READINGS_PER_FILE readings per file, then rotates to a new file.
    passive component RadiationPayload {

        # ------------------------------------------------------------------
        # Events
        # ------------------------------------------------------------------

        @ A single reading was successfully written to disk
        event ReadingComplete(readingNum: U32, fileNum: U32) \
            severity activity low format "Reading {} stored in file {}"

        @ A file reached the reading limit and was closed; next file opened
        event FileComplete(fileNum: U32, readings: U32) \
            severity activity low format "File {} complete with {} readings, rotating to new file"

        @ A file could not be opened or written
        event FileError(description: string) \
            severity warning high format "File error: {}"

        @ A protocol transfer error occurred (bad status or write failure)
        event TransferError(description: string) \
            severity warning high format "Transfer error: {}"

        @ A complete gamma reading was received and stored
        event GammaReadingReceived(bytes: U32, path: string) \
            severity activity high format "Received {} bytes, stored in {}"

        # ------------------------------------------------------------------
        # Telemetry
        # ------------------------------------------------------------------

        @ Readings written to the current open file
        telemetry ReadingsInCurrentFile: U32

        @ Total files completed this session
        telemetry FilesWritten: U32

        @ Total readings stored across all files this session
        telemetry TotalReadings: U32

        # ------------------------------------------------------------------
        # Parameters
        # ------------------------------------------------------------------

        @ Readings per file before rotating to a new file (default: 100)
        param READINGS_PER_FILE: U32 default 100

        # ------------------------------------------------------------------
        # Ports
        # ------------------------------------------------------------------

        @ Receives raw byte stream from MOSAIC via PayloadCom (UART RX)
        sync input port dataIn: Drv.ByteStreamData

        ###############################################################################
        # Standard AC Ports: Required for Channels, Events, Commands, and Parameters  #
        ###############################################################################
        @ Port for requesting the current time
        time get port timeCaller

        @ Port for sending command registrations
        command reg port cmdRegOut

        @ Port for receiving commands
        command recv port cmdIn

        @ Port for sending command responses
        command resp port cmdResponseOut

        @ Port for sending textual representation of events
        text event port logTextOut

        @ Port for sending events to downlink
        event port logOut

        @ Port for sending telemetry channels to downlink
        telemetry port tlmOut

        @ Port to return the value of a parameter
        param get port prmGetOut

        @ Port to set the value of a parameter
        param set port prmSetOut

    }
}
