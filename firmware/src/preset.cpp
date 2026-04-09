#include "preset.h"
#include <cstring>
#include "movement.h"

namespace preset {

namespace {

constexpr const char* kNamespace = "PresetsV1";
constexpr const char* kKeyVersion = "version";
constexpr const char* kKeyTable = "table";
constexpr uint8_t kStorageVersion = 1;

Preset gPresets[kMaxPresets] = {};
bool gInitialized = false;

void setName(char* destination, const char* source) {
	if (destination == nullptr) {
		return;
	}
	destination[0] = '\0';
	if (source == nullptr) {
		return;
	}
	std::strncpy(destination, source, kMaxNameLen - 1);
	destination[kMaxNameLen - 1] = '\0';
}

void clearPreset(Preset& entry, uint8_t id) {
	entry.used = false;
	entry.id = id;
	entry.steps = 0;
	entry.name[0] = '\0';
}

void resetTable() {
	for (uint8_t i = 0; i < kMaxPresets; ++i) {
		clearPreset(gPresets[i], i + 1);
	}
}

bool saveTable() {
	Preferences preferences;
	if (!preferences.begin(kNamespace, false)) {
		return false;
	}

	const size_t expectedBytes = sizeof(gPresets);
	const size_t writtenBytes = preferences.putBytes(kKeyTable, gPresets, expectedBytes);
	preferences.putUChar(kKeyVersion, kStorageVersion);
	preferences.end();
	return writtenBytes == expectedBytes;
}

bool isValidId(uint8_t id) {
	return id >= 1 && id <= kMaxPresets;
}

} // namespace

bool begin() {
	if (gInitialized) {
		return true;
	}

	resetTable();

	Preferences preferences;
	if (!preferences.begin(kNamespace, true)) {
		gInitialized = true;
		return true;
	}

	const uint8_t version = preferences.getUChar(kKeyVersion, 0);
	if (version == kStorageVersion) {
		const size_t expectedBytes = sizeof(gPresets);
		const size_t readBytes = preferences.getBytes(kKeyTable, gPresets, expectedBytes);
		if (readBytes != expectedBytes) {
			resetTable();
		}
	}
	preferences.end();

	gInitialized = true;
	return true;
}

uint8_t capacity() {
	return kMaxPresets;
}

bool getByIndex(uint8_t index, Preset& outPreset) {
	if (!gInitialized) {
		begin();
	}
	if (index >= kMaxPresets) {
		return false;
	}
	outPreset = gPresets[index];
	return true;
}

bool getById(uint8_t id, Preset& outPreset) {
	if (!gInitialized) {
		begin();
	}
	if (!isValidId(id)) {
		return false;
	}
	const uint8_t index = static_cast<uint8_t>(id - 1);
	if (!gPresets[index].used) {
		return false;
	}
	outPreset = gPresets[index];
	return true;
}

bool add(const char* name, int32_t steps, uint8_t& outId) {
	if (!gInitialized) {
		begin();
	}

	for (uint8_t i = 0; i < kMaxPresets; ++i) {
		if (gPresets[i].used) {
			continue;
		}
		gPresets[i].used = true;
		gPresets[i].id = i + 1;
		gPresets[i].steps = steps;
		setName(gPresets[i].name, name);
		if (!saveTable()) {
			clearPreset(gPresets[i], i + 1);
			return false;
		}
		outId = gPresets[i].id;
		return true;
	}

	return false;
}

bool set(uint8_t id, const char* name, int32_t steps) {
	if (!gInitialized) {
		begin();
	}
	if (!isValidId(id)) {
		return false;
	}

	const uint8_t index = static_cast<uint8_t>(id - 1);
	if (!gPresets[index].used) {
		return false;
	}

	gPresets[index].steps = steps;
	setName(gPresets[index].name, name);
	return saveTable();
}

bool remove(uint8_t id) {
	if (!gInitialized) {
		begin();
	}
	if (!isValidId(id)) {
		return false;
	}

	const uint8_t index = static_cast<uint8_t>(id - 1);
	if (!gPresets[index].used) {
		return false;
	}

	clearPreset(gPresets[index], id);
	return saveTable();
}

bool gotoById(uint8_t id) {
	if (!gInitialized) {
		begin();
	}
	if (!isValidId(id)) {
		return false;
	}

	const uint8_t index = static_cast<uint8_t>(id - 1);
	if (!gPresets[index].used) {
		return false;
	}

	Movement::moveToPosition(gPresets[index].steps);
	return true;
}

size_t count() {
	if (!gInitialized) {
		begin();
	}

	size_t usedCount = 0;
	for (uint8_t i = 0; i < kMaxPresets; ++i) {
		if (gPresets[i].used) {
			++usedCount;
		}
	}
	return usedCount;
}

} // namespace preset
