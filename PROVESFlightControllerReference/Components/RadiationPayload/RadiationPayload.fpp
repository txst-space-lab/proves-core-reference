module Components {
    @ Component that manages MOSAIC radiation readings using F Prime Data Products
    passive component RadiationPayload {

        @ Start collecting radiation readings
        sync command START_PAYLOAD()

        @ Stop collecting radiation readings; flushes any buffered readings
        sync command STOP_PAYLOAD()

        @ Immediately flush any buffered readings into a data product
        sync command TAKE_GAMMA_READING()

        @ Number of readings buffered since last flush
        telemetry ReadingsCollected: FwSizeType

        @ Number of data products sent to the catalog
        telemetry DataProductsSent: FwSizeType

        @ Most recent gamma radiation reading received from MOSAIC
        telemetry GammaRadiationReading: F64

        @ Events
        event PayloadStarted() severity activity high format "Radiation payload started"
        event PayloadStopped() severity activity high format "Radiation payload stopped"
        event DataProductSent(count: U32) severity activity low format "Sent data product containing {} readings"
        event DataProductError() severity warning high format "Failed to acquire data product buffer; readings retained"

        @ Number of readings to batch per data product (default 100)
        param READINGS_PER_FILE: U32 default 100

        @ Periodic schedule port driven by a rate group
        sync input port run: Svc.Sched

        @ Raw UART data from MOSAIC radiation sensor via PayloadCom
        sync input port dataIn: Drv.ByteStreamData

        ###############################################################################
        # Data Product Ports                                                          #
        ###############################################################################

        @ Synchronous port to request a data product buffer
        product get port productGetOut

        @ Port to send a completed data product to the catalog
        product send port productSendOut

        @ One gamma radiation reading (F64) per record
        product record GammaReading: F64 id 1

        @ Container batching READINGS_PER_FILE GammaReading records
        product container RadiationData id 1 default priority 10

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

        @ Port for reading parameters
        param get port prmGetOut

        @ Port for setting parameters
        param set port prmSetOut

    }
}
