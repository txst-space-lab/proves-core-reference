module Components {
    @ Passive component that reads radiation data from the MOSAIC payload and stores it on disk.
    @ MOSAIC sends framed readings using <GAMMA_START><SIZE>[4-byte LE uint32]</SIZE>[data] protocol.
    @ Collects READINGS_PER_FILE readings per file, then rotates to a new file.
    passive component RadiationPayload {

        # ------------------------------------------------------------------
        # Commands
        # ------------------------------------------------------------------

        @ Power on the payload and begin collecting readings continuously
        sync command POWER_ON()

        @ Power off the payload and stop collecting readings
        sync command POWER_OFF()

        # ------------------------------------------------------------------
        # Events
        # ------------------------------------------------------------------

        @ Payload powered on and first reading requested
        event PayloadOn() severity activity high format "Radiation payload powered on"

        @ Payload powered off
        event PayloadOff(totalReadings: U32) \
            severity activity high format "Radiation payload powered off, {} readings stored this session"

        @ POWER_ON received but payload was already on
        event AlreadyOn() severity warning low format "Radiation payload is already on"

        @ POWER_OFF received but payload was already off
        event AlreadyOff() severity warning low format "Radiation payload is already off"

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

        @ Whether the payload is currently powered on and collecting
        telemetry PowerState: bool

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

        output port turnOnPayload: Fw.Signal

        output port turnOffPayload: Fw.Signal

        @ Receives raw byte stream from MOSAIC via PayloadCom (UART RX)
        sync input port dataIn: Drv.ByteStreamData

        @ Sends commands and ACKs to MOSAIC via PayloadCom (UART TX)
        output port commandOut: Drv.ByteStreamData

        @ Rate group connection for periodic telemetry
        sync input port run: Svc.Sched

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
