// Xbox USB controller -> Red Robot Xbox protocol over Bluetooth Low Energy.
import Foundation
import CoreBluetooth
import GameController
import Darwin

let robotName = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "RR-11-platypus"
let serviceUUID = CBUUID(string: "6E400001-B5A3-F393-E0A9-E50E24DCCA9E")
let rxUUID = CBUUID(string: "6E400002-B5A3-F393-E0A9-E50E24DCCA9E")
let txUUID = CBUUID(string: "6E400003-B5A3-F393-E0A9-E50E24DCCA9E")
let firmwareID = Data("RR26-XBOX-3".utf8)
let neutralPacket = Data([33, 88, 100, 100, 1, 1])
let autonomousPacket = Data([33, 88, 100, 100, 1, 1, 1])

final class RobotBridge: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    var central: CBCentralManager!
    var robot: CBPeripheral?
    var rx: CBCharacteristic?
    var tx: CBCharacteristic?
    var controller: GCController?
    var timer: Timer?
    var pending: Data?
    var writing = false
    var lastMenu = true
    var autonomous = false
    var readyForInput = false
    var lastAcknowledgedAt = Date()
    var running = true

    func start() {
        print("Waiting for a USB Xbox controller and Bluetooth. Robot: \(robotName)")
        print("Left stick/D-pad: drive. Right stick: ramp up/down and claw left/right.")
        print("Both sticks work simultaneously; no mode buttons or RB are needed.")
        print("Press Menu to start autonomous; View stops it.")
        print("Keep the robot clear: a Bluetooth disconnect can affect its mode.")
        central = CBCentralManager(delegate: self, queue: .main)
        timer = Timer.scheduledTimer(withTimeInterval: 0.05, repeats: true) { [weak self] _ in
            self?.poll()
        }
    }

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn:
            print("Bluetooth is ready.")
        case .unauthorized:
            print("Bluetooth access denied. Enable Bluetooth access for Terminal in macOS settings.")
        case .poweredOff:
            print("Bluetooth is off. Turn it on in macOS settings.")
        default:
            print("Bluetooth state: \(central.state.rawValue)")
        }
    }

    func poll() {
        guard running else { return }
        if controller == nil {
            controller = GCController.controllers().first { $0.extendedGamepad != nil }
            if let controller {
                print("Controller detected: \(controller.vendorName ?? "game controller")")
            }
        } else if !GCController.controllers().contains(where: { $0 === controller }) {
            print("Controller disconnected; commanding stop.")
            autonomous = false
            readyForInput = false
            send(neutralPacket)
            controller = nil
            lastMenu = true
        }

        guard let gamepad = controller?.extendedGamepad else {
            if rx != nil { send(neutralPacket) }
            return
        }
        if central.state == .poweredOn && robot == nil && !central.isScanning {
            print("Looking for \(robotName)...")
            central.scanForPeripherals(withServices: [serviceUUID])
        }
        guard rx != nil else { return }

        // A missed heartbeat makes the firmware stop and require a neutral
        // packet before it accepts controls again. Do not resume autonomy.
        if Date().timeIntervalSince(lastAcknowledgedAt) > 0.2 {
            if autonomous { print("Bluetooth delayed; autonomous must be restarted with Menu.") }
            autonomous = false
            readyForInput = false
            send(neutralPacket)
            return
        }

        let menu = gamepad.buttonMenu.isPressed
        let view = gamepad.buttonOptions?.isPressed ?? false
        let up = gamepad.dpad.up.isPressed
        let down = gamepad.dpad.down.isPressed
        let left = gamepad.dpad.left.isPressed
        let right = gamepad.dpad.right.isPressed
        let stickX = gamepad.leftThumbstick.xAxis.value
        let stickY = gamepad.leftThumbstick.yAxis.value
        let rightStickX = gamepad.rightThumbstick.xAxis.value
        let rightStickY = gamepad.rightThumbstick.yAxis.value
        if view {
            if autonomous { print("Mode: manual (View)") }
            autonomous = false
            readyForInput = false
            lastMenu = menu
            send(neutralPacket)
            return
        }
        if menu && !lastMenu {
            autonomous = true
            readyForInput = false
            print("Requested mode: autonomous (Menu)")
        }
        lastMenu = menu
        if autonomous {
            send(autonomousPacket)
            return
        }
        if !readyForInput {
            send(neutralPacket)
            if !up && !down && !left && !right && abs(stickX) < 0.3 && abs(stickY) < 0.3 &&
                abs(rightStickX) < 0.3 && abs(rightStickY) < 0.3 {
                readyForInput = true
            }
            return
        }

        func driveAxis(_ value: Float) -> Int {
            let deadZone: Float = 0.25
            let scaled = min(1, max(0, (abs(value) - deadZone) / (1 - deadZone)))
            return Int((scaled * 100).rounded()) * (value < 0 ? -1 : 1)
        }
        let drive = up || down ? Int(up ? 100 : 0) - Int(down ? 100 : 0) : driveAxis(stickY)
        let turn = left || right ? Int(right ? 100 : 0) - Int(left ? 100 : 0) : driveAxis(stickX)
        let ramp = Int(rightStickY > 0.5 ? 1 : 0) - Int(rightStickY < -0.5 ? 1 : 0)
        let claw = Int(rightStickX > 0.5 ? 1 : 0) - Int(rightStickX < -0.5 ? 1 : 0)
        send(Data([33, 88, UInt8(turn + 100), UInt8(drive + 100), UInt8(ramp + 1), UInt8(claw + 1)]))
    }

    func send(_ packet: Data) {
        guard rx != nil else { return }
        // Keep only the latest state while a write is in flight.
        pending = packet
        writeNext()
    }

    func writeNext() {
        guard !writing, let packet = pending, let robot, let rx else { return }
        pending = nil
        writing = true
        robot.writeValue(packet, for: rx, type: .withResponse)
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any], rssi RSSI: NSNumber) {
        let name = (advertisementData[CBAdvertisementDataLocalNameKey] as? String) ?? peripheral.name
        guard name == robotName else { return }
        print("Found \(robotName); connecting...")
        central.stopScan()
        robot = peripheral
        peripheral.delegate = self
        central.connect(peripheral)
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        print("Connected; discovering robot controls...")
        peripheral.discoverServices([serviceUUID])
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral,
                        error: Error?) {
        print("Connection failed: \(error?.localizedDescription ?? "unknown error")")
        robot = nil
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral,
                        error: Error?) {
        print("Robot disconnected. Robot firmware may resume autonomous driving.")
        rx = nil
        tx = nil
        robot = nil
        pending = nil
        writing = false
        autonomous = false
        readyForInput = false
        lastMenu = true
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard error == nil, let service = peripheral.services?.first(where: { $0.uuid == serviceUUID }) else {
            print("Robot control service not found.")
            central.cancelPeripheralConnection(peripheral)
            return
        }
        peripheral.discoverCharacteristics([rxUUID, txUUID], for: service)
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService,
                    error: Error?) {
        guard error == nil, let writeCharacteristic = service.characteristics?.first(where: { $0.uuid == rxUUID }),
              writeCharacteristic.properties.contains(.write),
              let readCharacteristic = service.characteristics?.first(where: { $0.uuid == txUUID }),
              readCharacteristic.properties.contains(.read) else {
            print("Compatible Xbox firmware not found; robot will not be controlled.")
            central.cancelPeripheralConnection(peripheral)
            return
        }
        tx = readCharacteristic
        // Do not enable controls until the firmware identification matches.
        peripheral.readValue(for: readCharacteristic)
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic,
                    error: Error?) {
        guard characteristic.uuid == txUUID, error == nil, characteristic.value == firmwareID,
              let service = peripheral.services?.first(where: { $0.uuid == serviceUUID }),
              let writeCharacteristic = service.characteristics?.first(where: { $0.uuid == rxUUID }) else {
            print("Xbox firmware verification failed; robot will not be controlled.")
            central.cancelPeripheralConnection(peripheral)
            return
        }
        rx = writeCharacteristic
        autonomous = false
        readyForInput = false
        lastMenu = true
        lastAcknowledgedAt = Date()
        send(neutralPacket)
        print("Ready. Left stick/D-pad = drive; right stick = ramp/claw; Menu = autonomous; View = stop.")
    }

    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic,
                    error: Error?) {
        writing = false
        if let error {
            print("Robot command failed: \(error.localizedDescription). Stop the robot physically.")
            pending = nil
            autonomous = false
            readyForInput = false
            return
        }
        lastAcknowledgedAt = Date()
        writeNext()
    }
}

let bridge = RobotBridge()
bridge.start()
RunLoop.main.run()
