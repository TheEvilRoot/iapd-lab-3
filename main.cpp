#pragma comment (lib, "setupapi.lib")

#include <iostream>
#include <optional>

#include <Windows.h>
#include <SetupAPI.h>
#include <batclass.h>
#include <devguid.h>

struct Battery {
	size_t capacity;
	size_t designed_max_capacity;
	size_t full_charge_capacity;
	size_t voltage;
	size_t discharge_rate;

	UCHAR type[4];
};

std::optional<Battery> acquireBatteryInfo() {
	auto deviceHandle = SetupDiGetClassDevs(&GUID_DEVCLASS_BATTERY, nullptr, 0, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (deviceHandle == INVALID_HANDLE_VALUE) {
		std::cerr << "SetupDiGetClassDevs :: GUID_DEVCLASS_BATTERY : " << GetLastError() << "\n";
		return std::optional<Battery>();
	}

	SP_DEVICE_INTERFACE_DATA interfaceData{ };
	interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

	if (!SetupDiEnumDeviceInterfaces(deviceHandle, 0, &GUID_DEVCLASS_BATTERY, 0, &interfaceData)) {
		if (GetLastError() == ERROR_NO_MORE_ITEMS) {
			std::cerr << "acquireBatteryInfo :: No battery found\n";
		} else {
			std::cerr << "SetupDiEnumDeviceInterfaces :: " << GetLastError() << "\n";
		}
		return std::optional<Battery>();
	}

	auto buffer = std::make_unique<unsigned char[]>(120);
	SP_DEVICE_INTERFACE_DETAIL_DATA detailData{};
	detailData.cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

	if (!SetupDiGetDeviceInterfaceDetail(deviceHandle, &interfaceData, &detailData, sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA), 0, 0)) {
		std::cerr << "SetupDiGetDeviceInterfaceDetail :: " << GetLastError() << "\n";
		return std::optional<Battery>();
	}

	auto batteryHandle = CreateFile(detailData.DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, nullptr);
	if (batteryHandle == INVALID_HANDLE_VALUE) {
		std::cerr << "CreateFile :: " << std::string(detailData.DevicePath) << " : " << GetLastError() << "\n";
		return std::optional<Battery>();
	}

	BATTERY_QUERY_INFORMATION queryInfo{};
	if (!DeviceIoControl(batteryHandle, IOCTL_BATTERY_QUERY_TAG, nullptr, 0, &queryInfo.BatteryTag, sizeof(BATTERY_QUERY_INFORMATION), nullptr, nullptr)) {
		std::cerr << "DeviceIoControl :: IOCTL_BATTERY_QUERY_TAG : " << GetLastError() << "\n";
		return std::optional<Battery>();
	}

	BATTERY_INFORMATION batteryInfo{};
	queryInfo.InformationLevel = BatteryInformation;

	if (!DeviceIoControl(batteryHandle, IOCTL_BATTERY_QUERY_INFORMATION, &queryInfo, sizeof(BATTERY_QUERY_INFORMATION), &batteryInfo, sizeof(BATTERY_INFORMATION), nullptr, nullptr)) {
		std::cerr << "DeviceIoControl :: IOCTL_BATTERY_QUERY_INFORMATION : " << GetLastError() << "\n";
		return std::optional<Battery>();
	}

	BATTERY_WAIT_STATUS waitStatus{};
	waitStatus.BatteryTag = queryInfo.BatteryTag;

	BATTERY_STATUS batteryStatus{};
	
	if (!DeviceIoControl(batteryHandle, IOCTL_BATTERY_QUERY_STATUS, &waitStatus, sizeof(BATTERY_WAIT_STATUS), &batteryStatus, sizeof(BATTERY_STATUS), nullptr, nullptr)) {
		std::cerr << "DeviceIoControl :: IOCTL_BATTERY_QUERY_STATUS : " << GetLastError() << "\n";
		return std::optional<Battery>();
	}

	Battery battery { batteryInfo.DesignedCapacity, batteryInfo.FullChargedCapacity, batteryStatus.Capacity, batteryStatus.Voltage, batteryStatus.Rate };
	std::memcpy(battery.type, batteryInfo.Chemistry, 4);

	return battery;
}

int main() {
	if (auto battery = acquireBatteryInfo(); battery) {
		auto b = battery.value();

		std::cout << b.capacity << "\n";
		std::cout << b.full_charge_capacity << "\n";
		std::cout << b.designed_max_capacity;
		std::cout << b.discharge_rate << "\n";
		std::cout << b.voltage << "\n";
		std::cout << b.type << "\n";
	}
	return 0;
}
