module Components {
    @ Component that manages MOSAIC radiation readings, stores files to disk
    passive component RadiationPayload {

        @ Commands to control the payload
        sync command START_PAYLOAD()
        sync command STOP_PAYLOAD()

        @ Optional direct command to trigger a single reading
        sync command TAKE_GAMMA_READING()

        @ Telemetry: number of readings collected since boot
        telemetry ReadingsCollected: FwSizeType

        @ Telemetry: number of files written
        telemetry FilesWritten: FwSizeType

        @ Events
        event PayloadStarted() severity activity high format "Radiation payload started"
        event PayloadStopped() severity activity high format "Radiation payload stopped"
        event FileSaved(path: string) severity activity low format "Saved readings to {}"
        event FileOperationError(path: string, op: string) severity warning high format "File operation {} failed for {}"

        @ Configurable parameter: number of readings per file
        param READINGS_PER_FILE: U32 default 100

        @ Periodic run hook (called from a rate group) used to take readings while enabled
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

    }
}