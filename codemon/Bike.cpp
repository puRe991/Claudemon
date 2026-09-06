#include "Bike.h"

BikeKind Bike::in_bag(const GameState& gs) {
	if (gs.item_count("ITEM_MACH_BIKE") > 0) return BikeKind::MACH;
	if (gs.item_count("ITEM_ACRO_BIKE") > 0) return BikeKind::ACRO;
	return BikeKind::NONE;
}

const char* Bike::item_id(BikeKind kind) {
	switch (kind) {
	case BikeKind::MACH: return "ITEM_MACH_BIKE";
	case BikeKind::ACRO: return "ITEM_ACRO_BIKE";
	default:             return "";
	}
}

const char* Bike::display_name(BikeKind kind) {
	switch (kind) {
	case BikeKind::MACH: return "MACHO-RAD";
	case BikeKind::ACRO: return "AKRO-RAD";
	default:             return "FAHRRAD";
	}
}

std::string Bike::sheet_for(BikeKind kind, bool female) {
	const std::string who = female ? "may" : "brendan";
	switch (kind) {
	case BikeKind::MACH: return "assets/overworld/people_" + who + "_mach_bike.png";
	case BikeKind::ACRO: return "assets/overworld/people_" + who + "_acro_bike.png";
	default:             return "assets/overworld/people_" + who + "_walking.png";
	}
}

bool Bike::can_ride_rail(int axis, DIR dir, BikeKind kind) {
	if (axis == 0) return true;               // not a rail: nothing to gate
	if (kind != BikeKind::ACRO) return false; // on foot or on the MACH BIKE
	if (axis == 1) return dir == DIR::N || dir == DIR::S;   // vertical rail
	return dir == DIR::E || dir == DIR::W;                  // horizontal rail
}

BikeResult Bike::toggle(const GameState& gs, bool indoors, bool surfing) {
	if (riding()) { dismount(); return BikeResult::DISMOUNTED; }
	// The refusals in the order the real game checks them: you can be told
	// "not here" even without owning a bike, but "you have no bike" is the
	// more useful answer, so ownership comes first.
	BikeKind have = in_bag(gs);
	if (have == BikeKind::NONE) return BikeResult::NO_BIKE;
	if (surfing) return BikeResult::SURFING;
	if (indoors) return BikeResult::INDOORS;
	this->kind = have;
	this->streak = 0;
	this->streak_dir = DIR::NONE;
	return BikeResult::MOUNTED;
}

bool Bike::dismount() {
	if (!riding()) return false;
	this->kind = BikeKind::NONE;
	this->streak = 0;
	this->streak_dir = DIR::NONE;
	return true;
}

void Bike::resume(const GameState& gs) {
	this->kind = in_bag(gs);
	this->streak = 0;
	this->streak_dir = DIR::NONE;
}

void Bike::on_step(DIR dir) {
	if (!riding()) return;
	// Turning throws the acceleration away, exactly like the original: the
	// MACH BIKE only builds speed on a straight run.
	if (dir != this->streak_dir) { this->streak_dir = dir; this->streak = 0; }
	if (this->streak < MACH_RAMP_STEPS) ++this->streak;
}

float Bike::step_interval(float walk_interval) const {
	if (!riding()) return walk_interval;
	if (this->kind == BikeKind::ACRO || this->streak < MACH_RAMP_STEPS)
		return walk_interval / 2.f;
	return walk_interval / 3.f;
}
