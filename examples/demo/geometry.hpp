#pragma once
// # Geometry
// Small value types. Their padding shows up in the layout view.

struct Vec3 { double x, y, z; };

double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

struct Particle { Vec3 pos; double mass; char tag; };
