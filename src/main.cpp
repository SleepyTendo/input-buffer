#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

// Button ids GD passes to handleButton: 1 = jump, 2 = left, 3 = right.
constexpr int kJump = 1;
constexpr int kMaxButton = 3;

static bool modEnabled() {
	return Mod::get()->getSettingValue<bool>("enabled");
}

static float bufferSeconds() {
	auto frames = Mod::get()->getSettingValue<int64_t>("extra-frames");
	return static_cast<float>(frames) / 60.f;
}

static bool shouldBuffer(int button) {
	if (button == kJump) return true;
	if (button > kJump && button <= kMaxButton)
		return Mod::get()->getSettingValue<bool>("buffer-movement");
	return false;
}

class $modify(BufferedLayer, GJBaseGameLayer) {
	struct PendingRelease {
		bool active = false;
		float remaining = 0.f;
	};

	struct Fields {
		// [0] = player 1, [1] = player 2; indexed by button id
		PendingRelease pending[2][kMaxButton + 1];
	};

	PendingRelease& slot(int button, bool isPlayer1) {
		return m_fields->pending[isPlayer1 ? 0 : 1][button];
	}

	void handleButton(bool down, int button, bool isPlayer1) {
		if (!modEnabled() || button < 0 || button > kMaxButton || !shouldBuffer(button)) {
			GJBaseGameLayer::handleButton(down, button, isPlayer1);
			return;
		}

		auto& p = slot(button, isPlayer1);

		if (!down) {
			// Hold the release back; the game keeps treating the button as pressed.
			p.active = true;
			p.remaining = bufferSeconds();
			return;
		}

		// New press while a buffered release is pending: release first so
		// orbs and pads see a fresh click instead of a continued hold.
		if (p.active) {
			p.active = false;
			GJBaseGameLayer::handleButton(false, button, isPlayer1);
		}
		GJBaseGameLayer::handleButton(true, button, isPlayer1);
	}

	void flushPending() {
		for (int player = 0; player < 2; ++player) {
			for (int button = 0; button <= kMaxButton; ++button) {
				auto& p = m_fields->pending[player][button];
				if (p.active) {
					p.active = false;
					GJBaseGameLayer::handleButton(false, button, player == 0);
				}
			}
		}
	}

	void update(float dt) {
		for (int player = 0; player < 2; ++player) {
			for (int button = 0; button <= kMaxButton; ++button) {
				auto& p = m_fields->pending[player][button];
				if (!p.active) continue;
				p.remaining -= dt;
				if (p.remaining <= 0.f) {
					p.active = false;
					GJBaseGameLayer::handleButton(false, button, player == 0);
				}
			}
		}
		GJBaseGameLayer::update(dt);
	}
};

class $modify(BufferedPlayLayer, PlayLayer) {
	bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
		if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
		if (modEnabled() && Mod::get()->getSettingValue<bool>("safe-mode")) {
			m_isTestMode = true;
		}
		return true;
	}

	void resetLevel() {
		static_cast<BufferedLayer*>(static_cast<GJBaseGameLayer*>(this))->flushPending();
		PlayLayer::resetLevel();
	}
};
