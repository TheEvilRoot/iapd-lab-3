#pragma comment (lib, "setupapi.lib")

#include <iostream>
#include <optional>
#include <string>

#include <Windows.h>
#include <SetupAPI.h>
#include <batclass.h>
#include <devguid.h>

std::string getBatteryState(BYTE flag) {
	if (flag == 128) return "No battery";
	if (flag == 0xff) return "Unknown state";
	if (flag == 0x0) return "Normal, not charging";
	bool isCharging = flag & 0x8;

	std::string levelState;
	if (flag & 0x1) levelState = "High";
	if (flag & 0x2) levelState = "Low";
	if (flag & 0x4) levelState = "Critical";

	std::string state;
	if (isCharging) state = "Charging";
	if (!levelState.empty())
		state = (isCharging ? state + " at " : "") + levelState + " level";
	return state;
}

std::string formatTime(size_t v) {
	if (v < 60) return std::to_string(v) + " seconds";

	size_t minutes = v / 60;
	if (minutes < 60) return std::to_string(minutes) + " minutes";

	size_t hours = minutes / 60;
	minutes = minutes % 60;

	if (hours < 24) return std::to_string(hours) + " hours and " + std::to_string(minutes) + " minutes";

	size_t days = hours / 24;
	hours = hours % 24;

	return std::to_string(days) + " days and " + std::to_string(hours) + " hours";
}

struct BatteryStatus {
	float voltage;

	bool acStatus;
	bool saverStatus;

	std::string currentState;
	int percentage;

	bool showTimeRemaining;
	std::string timeRemaining;

	bool showTimeFromFullCharge;
	std::string timeFromFullCharge;

	friend std::ostream& operator<<(std::ostream& o, const BatteryStatus& s) {
		std::cout << "AC is " << (s.acStatus ? "connected" : "disconnected") << "\n";
		std::cout << "Battery saver is " << (s.saverStatus ? "enabled" : "disabled") << "\n";
		std::cout << "\n";
		std::cout << "Current state: " << s.percentage << "% (" << s.currentState << ")\n";
		std::cout << "Current voltage: " << s.voltage << " V\n";
		std::cout << "\n";
		if (s.showTimeRemaining) {
			std::cout << "Estimate time remaining: " << s.timeRemaining << "\n";
		}
		if (s.showTimeFromFullCharge) {
			std::cout << "Time from full charge: " << s.timeFromFullCharge << "\n";
		}
		return o;
	}
};

struct Battery {
	HANDLE handle;
	BATTERY_QUERY_INFORMATION queryInfo;
	std::string type;

	friend std::ostream& operator<<(std::ostream& o, const Battery& b) {
		std::cout << "Battery type: " << b.type << "\n";
		return o;
	}

	std::optional<BatteryStatus> updateStatus() const {
		BATTERY_WAIT_STATUS waitStatus{};
		waitStatus.BatteryTag = queryInfo.BatteryTag;

		BATTERY_STATUS batteryStatus{};

		if (!DeviceIoControl(handle, IOCTL_BATTERY_QUERY_STATUS, &waitStatus, sizeof(BATTERY_WAIT_STATUS), &batteryStatus, sizeof(BATTERY_STATUS), nullptr, nullptr)) {
			std::cerr << "DeviceIoControl :: IOCTL_BATTERY_QUERY_STATUS : " << GetLastError() << "\n";
			return std::optional<BatteryStatus>();
		}

		SYSTEM_POWER_STATUS status{};
		if (!GetSystemPowerStatus(&status)) {
			std::cerr << "GetSystemPowerStatus :: " << GetLastError() << "\n";
			return std::optional<BatteryStatus>();
		}

		return BatteryStatus { 
			float(batteryStatus.Voltage) / 1000,
			status.ACLineStatus > 0, 
			status.SystemStatusFlag > 0,
			getBatteryState(status.BatteryFlag),
			status.BatteryLifePercent,
			status.BatteryLifeTime != 0xffffffff,
			formatTime(status.BatteryLifeTime),
			status.BatteryFullLifeTime != 0xffffffff,
			formatTime(status.BatteryFullLifeTime)
		};
	}
};

std::string getType(UCHAR* t) {
	std::string type(reinterpret_cast<char*>(t), 4);
	for (int i = 0; i < type.size(); i++)
		std::cout << std::hex << int(type[i]) << " ";
	std::cout << "\n" << std::dec;
	if (type == "PbAc") return "Lead Acid";
	if (type == "LION" || type == "Li-I") return "Li-ion";
	if (type == "NiCd") return "NiCad";
	if (type == "NiMH") return "Ni-MH";
	if (type == "NiZn") return "NiZn";
	if (type == "RAM") return "RAM (Rechargeable alkaline)";
	return "INVALID";
}

std::optional<Battery> acquireBattery() {
	auto deviceHandle = SetupDiGetClassDevs(&GUID_DEVCLASS_BATTERY, nullptr, 0, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (deviceHandle == INVALID_HANDLE_VALUE) {
		std::cerr << "SetupDiGetClassDevs :: GUID_DEVCLASS_BATTERY : " << GetLastError() << "\n";
		return std::optional<Battery>();
	}

	SP_DEVICE_INTERFACE_DATA interfaceData{ };
	interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

	if (!SetupDiEnumDeviceInterfaces(deviceHandle, nullptr, &GUID_DEVCLASS_BATTERY, 0, &interfaceData)) {
		if (GetLastError() == ERROR_NO_MORE_ITEMS) {
			std::cerr << "acquireBatteryInfo :: No battery found\n";
		} else {
			std::cerr << "SetupDiEnumDeviceInterfaces :: " << GetLastError() << "\n";
		}
		return std::optional<Battery>();
	}
	DWORD bufferSize = 0;
	SetupDiGetDeviceInterfaceDetail(deviceHandle, &interfaceData, nullptr, 0, &bufferSize, 0);

	auto buffer = std::make_unique<unsigned char[]>(bufferSize);
	SP_DEVICE_INTERFACE_DETAIL_DATA* detailData = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA*>(buffer.get());
	detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

	if (!SetupDiGetDeviceInterfaceDetail(deviceHandle, &interfaceData, detailData, bufferSize, 0, 0)) {
		std::cerr << "SetupDiGetDeviceInterfaceDetail :: " << GetLastError() << "\n";
		return std::optional<Battery>();
	}

	auto batteryHandle = CreateFileA(detailData->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, nullptr);
	if (batteryHandle == INVALID_HANDLE_VALUE) {
		std::cerr << "CreateFile :: " << std::string(detailData->DevicePath) << " : " << GetLastError() << "\n";
		return std::optional<Battery>();
	}

	BATTERY_QUERY_INFORMATION queryInfo{};
	if (!DeviceIoControl(batteryHandle, IOCTL_BATTERY_QUERY_TAG, nullptr, 0, &queryInfo.BatteryTag, sizeof(queryInfo.BatteryTag), nullptr, nullptr)) {
		std::cerr << "DeviceIoControl :: IOCTL_BATTERY_QUERY_TAG : " << GetLastError() << "\n";
		return std::optional<Battery>();
	}

	BATTERY_INFORMATION batteryInfo{};
	queryInfo.InformationLevel = BatteryInformation;

	if (!DeviceIoControl(batteryHandle, IOCTL_BATTERY_QUERY_INFORMATION, &queryInfo, sizeof(BATTERY_QUERY_INFORMATION), &batteryInfo, sizeof(BATTERY_INFORMATION), nullptr, nullptr)) {
		std::cerr << "DeviceIoControl :: IOCTL_BATTERY_QUERY_INFORMATION : " << GetLastError() << "\n";
		return std::optional<Battery>();
	}

	return Battery{ batteryHandle, queryInfo, getType(batteryInfo.Chemistry) };
}

int main() {
	if (auto b = acquireBattery(); b) {
		const auto& battery = b.value();

		while (true) {
			if (auto u = battery.updateStatus(); u) {
				system("cls");
				std::cout << battery << "\n";
				std::cout << u.value() << "\n";
			}
			Sleep(1000);
		}
	}

	getchar();
}
