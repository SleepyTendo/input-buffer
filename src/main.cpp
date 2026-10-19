#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

// How it works
// ------------
// If you click while a click can't do anything yet (cube/robot/ball/spider in
// the air and not touching an orb), the click is remembered for N frames.
// We keep the button held for you until the moment it can be used (you land,
// or you reach an orb), let the game use it exactly once, and then release it
// right away so it never turns into an extra jump. If the window runs out
// first, the click is simply dropped.

constexpr int kJump = 1;

static bool modEnabled() {
	return Mod::get()->getSettingValue<bool>("enabled");
}

static float windowSeconds() {
	auto frames = Mod::get()->getSettingValue<int64_t>("extra-frames");
	return static_cast<float>(frames) / 60.f;
}

// Gamemodes where a click in mid-air already does something on its own,
// so there is nothing to buffer.
static bool clickAlwaysActs(PlayerObject* p) {
	return p->m_isShip || p->m_isDart || p->m_isBird || p->m_isSwing;
}

static bool canUseClickNow(PlayerObject* p) {
	if (!p) return false;
	if (clickAlwaysActs(p)) return true;
	if (p->m_isOnGround) return true;
	if (p->m_touchingRings && p->m_touchingRings->count() > 0) return true;
	return false;
}

struct PlayerSnapshot {
	bool valid = false;
	bool onGround = false;
	bool upsideDown = false;
	bool dashing = false;
	size_t ringsUsed = 0;
};

static PlayerSnapshot snapshot(PlayerObject* p) {
	PlayerSnapshot s;
	if (!p) return s;
	s.valid = true;
	s.onGround = p->m_isOnGround;
	s.upsideDown = p->m_isUpsideDown;
	s.dashing = p->m_isDashing;
	s.ringsUsed = p->m_touchedRings.size();
	return s;
}

// Did this player just do something with the held click during the last step?
static bool clickWasUsed(PlayerSnapshot const& before, PlayerObject* p) {
	if (!before.valid || !p) return false;
	if (p->m_touchedRings.size() > before.ringsUsed) return true;   // hit an orb
	if (!before.dashing && p->m_isDashing) return true;            // dash orb
	if (before.onGround && !p->m_isOnGround) return true;         // jumped off the ground
	if (before.upsideDown != p->m_isUpsideDown) return true;      // ball / spider flip
	return false;
}

class $modify(BufferedLayer, GJBaseGameLayer) {
	struct Pending {
		bool active = false;    // an early click is being buffered
		bool released = false;  // the real button is already up; we're holding it for you
		float remaining = 0.f;
		bool isPlayer1 = true;  // the flag the game expects for this input
	};

	struct Fields {
		Pending pending[2];
	};

	bool isTwoPlayer() {
		return m_levelSettings && m_levelSettings->m_twoPlayerMode;
	}

	int slotFor(bool isPlayer1) {
		return (isTwoPlayer() && !isPlayer1) ? 1 : 0;
	}

	// The players a click from this slot controls.
	std::vector<PlayerObject*> playersFor(int slot) {
		std::vector<PlayerObject*> out;
		if (isTwoPlayer()) {
			auto p = slot == 0 ? m_player1 : m_player2;
			if (p) out.push_back(p);
		}
		else {
			if (m_player1) out.push_back(m_player1);
			if (m_player2 && m_gameState.m_isDualMode) out.push_back(m_player2);
		}
		return out;
	}

	void releaseSlot(int slot) {
		auto& p = m_fields->pending[slot];
		bool needsRelease = p.active && p.released;
		p.active = false;
		p.released = false;
		if (needsRelease) GJBaseGameLayer::handleButton(false, kJump, p.isPlayer1);
	}

	void handleButton(bool down, int button, bool isPlayer1) {
		if (!modEnabled() || button != kJump) {
			GJBaseGameLayer::handleButton(down, button, isPlayer1);
			return;
		}

		int slot = slotFor(isPlayer1);
		auto& p = m_fields->pending[slot];

		if (!down) {
			if (p.active) {
				// Keep holding until the click gets used or the window ends.
				p.released = true;
				return;
			}
			GJBaseGameLayer::handleButton(false, button, isPlayer1);
			return;
		}

		// A new click while we're still holding an old one: let go first
		// so the game sees a fresh press.
		if (p.active && p.released) releaseSlot(slot);

		GJBaseGameLayer::handleButton(true, button, isPlayer1);

		bool usableNow = false;
		for (auto player : playersFor(slot)) {
			if (canUseClickNow(player)) usableNow = true;
		}

		if (usableNow) {
			p.active = false;
			p.released = false;
		}
		else {
			p.active = true;
			p.released = false;
			p.remaining = windowSeconds();
			p.isPlayer1 = isPlayer1;
		}
	}

	void processCommands(float dt, bool isHalfTick, bool isLastTick) {
		auto& f = m_fields;
		bool anyActive = f->pending[0].active || f->pending[1].active;
		if (!modEnabled() || !anyActive) {
			GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
			return;
		}

		std::vector<std::pair<PlayerObject*, PlayerSnapshot>> before[2];
		for (int slot = 0; slot < 2; ++slot) {
			if (!f->pending[slot].active) continue;
			for (auto player : playersFor(slot)) {
				before[slot].push_back({ player, snapshot(player) });
			}
		}

		GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);

		for (int slot = 0; slot < 2; ++slot) {
			auto& p = f->pending[slot];
			if (!p.active) continue;

			bool used = false;
			for (auto& [player, snap] : before[slot]) {
				if (clickWasUsed(snap, player)) used = true;
			}

			if (used) {
				// The buffered click did its one job; stop holding right away.
				releaseSlot(slot);
				continue;
			}

			p.remaining -= dt;
			if (p.remaining <= 0.f) releaseSlot(slot);
		}
	}

	void clearBuffers() {
		releaseSlot(0);
		releaseSlot(1);
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
		static_cast<BufferedLayer*>(static_cast<GJBaseGameLayer*>(this))->clearBuffers();
		PlayLayer::resetLevel();
	}
};
