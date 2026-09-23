// MTX/VEC: C implementations of the paired-single matrix library. Matrices are
// row-major 3x4 (implicit last row 0 0 0 1) acting on column vectors, as in the
// SDK; projection matrices follow the GX clip-space convention (z in [-w, 0]).
#include "port_compat.h"
#include <dolphin/mtx.h>

#define DEF2(name, args, ...) \
	extern "C" void C_##name args __VA_ARGS__ extern "C" void PS##name args __VA_ARGS__

extern "C" {
void PSMTXMultVecSR(Mtx m, Vec* src, Vec* dst);
void C_MTXMultVecSR(Mtx m, Vec* src, Vec* dst);
}

static void identity(Mtx m)
{
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 4; j++)
			m[i][j] = i == j ? 1.0f : 0.0f;
}
static void concat(Mtx a, Mtx b, Mtx ab)
{
	Mtx t;
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 4; j++)
			t[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] + (j == 3 ? a[i][3] : 0.0f);
	}
	memcpy(ab, t, sizeof(Mtx));
}
static u32 inverse(Mtx src, Mtx inv)
{
	f32 a = src[0][0], b = src[0][1], c = src[0][2];
	f32 d = src[1][0], e = src[1][1], f = src[1][2];
	f32 g = src[2][0], h = src[2][1], i = src[2][2];
	f32 det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
	if (det == 0.0f)
		return 0;
	f32 r = 1.0f / det;
	Mtx t;
	t[0][0] = (e * i - f * h) * r;
	t[0][1] = (c * h - b * i) * r;
	t[0][2] = (b * f - c * e) * r;
	t[1][0] = (f * g - d * i) * r;
	t[1][1] = (a * i - c * g) * r;
	t[1][2] = (c * d - a * f) * r;
	t[2][0] = (d * h - e * g) * r;
	t[2][1] = (b * g - a * h) * r;
	t[2][2] = (a * e - b * d) * r;
	for (int k = 0; k < 3; k++)
		t[k][3] = -(t[k][0] * src[0][3] + t[k][1] * src[1][3] + t[k][2] * src[2][3]);
	memcpy(inv, t, sizeof(Mtx));
	return 1;
}
static void rottrig(Mtx m, char axis, f32 s, f32 c)
{
	identity(m);
	switch (axis) {
	case 'x':
	case 'X':
		m[1][1] = c;
		m[1][2] = -s;
		m[2][1] = s;
		m[2][2] = c;
		break;
	case 'y':
	case 'Y':
		m[0][0] = c;
		m[0][2] = s;
		m[2][0] = -s;
		m[2][2] = c;
		break;
	case 'z':
	case 'Z':
		m[0][0] = c;
		m[0][1] = -s;
		m[1][0] = s;
		m[1][1] = c;
		break;
	}
}
static void rotaxis(Mtx m, Vec* axis, f32 rad)
{
	f32 s = sinf(rad), c = cosf(rad), t = 1.0f - c;
	f32 len = sqrtf(axis->x * axis->x + axis->y * axis->y + axis->z * axis->z);
	f32 x = axis->x / len, y = axis->y / len, z = axis->z / len;
	m[0][0] = t * x * x + c;
	m[0][1] = t * x * y - s * z;
	m[0][2] = t * x * z + s * y;
	m[0][3] = 0;
	m[1][0] = t * x * y + s * z;
	m[1][1] = t * y * y + c;
	m[1][2] = t * y * z - s * x;
	m[1][3] = 0;
	m[2][0] = t * x * z - s * y;
	m[2][1] = t * y * z + s * x;
	m[2][2] = t * z * z + c;
	m[2][3] = 0;
}
static void quat(Mtx m, Quaternion* q)
{
	f32 n = q->x * q->x + q->y * q->y + q->z * q->z + q->w * q->w;
	f32 s = n > 0.0f ? 2.0f / n : 0.0f;
	f32 xs = q->x * s, ys = q->y * s, zs = q->z * s;
	f32 wx = q->w * xs, wy = q->w * ys, wz = q->w * zs;
	f32 xx = q->x * xs, xy = q->x * ys, xz = q->x * zs;
	f32 yy = q->y * ys, yz = q->y * zs, zz = q->z * zs;
	m[0][0] = 1.0f - (yy + zz);
	m[0][1] = xy - wz;
	m[0][2] = xz + wy;
	m[0][3] = 0;
	m[1][0] = xy + wz;
	m[1][1] = 1.0f - (xx + zz);
	m[1][2] = yz - wx;
	m[1][3] = 0;
	m[2][0] = xz - wy;
	m[2][1] = yz + wx;
	m[2][2] = 1.0f - (xx + yy);
	m[2][3] = 0;
}

DEF2(MTXIdentity, (Mtx m), { identity(m); })
DEF2(MTXCopy, (Mtx s, Mtx d), { if (s != d) memcpy(d, s, sizeof(Mtx)); })
DEF2(MTXConcat, (Mtx a, Mtx b, Mtx ab), { concat(a, b, ab); })
extern "C" u32 C_MTXInverse(Mtx s, Mtx i) { return inverse(s, i); }
extern "C" u32 PSMTXInverse(Mtx s, Mtx i) { return inverse(s, i); }
DEF2(MTXTranspose, (Mtx s, Mtx x), {
	Mtx t;
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			t[i][j] = s[j][i];
	t[0][3] = t[1][3] = t[2][3] = 0;
	memcpy(x, t, sizeof(Mtx));
})
DEF2(MTXRotRad, (Mtx m, char axis, f32 rad), { rottrig(m, axis, sinf(rad), cosf(rad)); })
DEF2(MTXRotTrig, (Mtx m, char axis, f32 s, f32 c), { rottrig(m, axis, s, c); })
DEF2(MTXRotAxisRad, (Mtx m, Vec* axis, f32 rad), { rotaxis(m, axis, rad); })
DEF2(MTXQuat, (Mtx m, Quaternion* q), { quat(m, q); })
DEF2(MTXTrans, (Mtx m, f32 x, f32 y, f32 z), {
	identity(m);
	m[0][3] = x;
	m[1][3] = y;
	m[2][3] = z;
})
DEF2(MTXTransApply, (Mtx s, Mtx d, f32 x, f32 y, f32 z), {
	if (s != d)
		memcpy(d, s, sizeof(Mtx));
	d[0][3] += x;
	d[1][3] += y;
	d[2][3] += z;
})
DEF2(MTXScale, (Mtx m, f32 x, f32 y, f32 z), {
	identity(m);
	m[0][0] = x;
	m[1][1] = y;
	m[2][2] = z;
})
DEF2(MTXScaleApply, (Mtx s, Mtx d, f32 x, f32 y, f32 z), {
	f32 k[3] = { x, y, z };
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 4; j++)
			d[i][j] = s[i][j] * k[i];
})
static void multvec(f32 (*m)[4], Vec* s, Vec* d)
{
	Vec t;
	t.x = m[0][0] * s->x + m[0][1] * s->y + m[0][2] * s->z + m[0][3];
	t.y = m[1][0] * s->x + m[1][1] * s->y + m[1][2] * s->z + m[1][3];
	t.z = m[2][0] * s->x + m[2][1] * s->y + m[2][2] * s->z + m[2][3];
	*d  = t;
}
static void multvecsr(f32 (*m)[4], Vec* s, Vec* d)
{
	Vec t;
	t.x = m[0][0] * s->x + m[0][1] * s->y + m[0][2] * s->z;
	t.y = m[1][0] * s->x + m[1][1] * s->y + m[1][2] * s->z;
	t.z = m[2][0] * s->x + m[2][1] * s->y + m[2][2] * s->z;
	*d  = t;
}
extern "C" void C_MTXMultVec(Mtx44 m, Vec* s, Vec* d) { multvec(m, s, d); }
extern "C" void PSMTXMultVec(Mtx44 m, Vec* s, Vec* d) { multvec(m, s, d); }
extern "C" void C_MTXMultVecSR(Mtx m, Vec* s, Vec* d) { multvecsr(m, s, d); }
extern "C" void PSMTXMultVecSR(Mtx m, Vec* s, Vec* d) { multvecsr(m, s, d); }
DEF2(MTXMultVecArray, (Mtx m, Vec* s, Vec* d, u32 n), {
	for (u32 i = 0; i < n; i++)
		multvec(m, &s[i], &d[i]);
})

// --- VEC ---
DEF2(VECAdd, (Vec* a, Vec* b, Vec* c), {
	c->x = a->x + b->x;
	c->y = a->y + b->y;
	c->z = a->z + b->z;
})
DEF2(VECSubtract, (Vec* a, Vec* b, Vec* c), {
	c->x = a->x - b->x;
	c->y = a->y - b->y;
	c->z = a->z - b->z;
})
DEF2(VECScale, (Vec* s, Vec* d, f32 k), {
	d->x = s->x * k;
	d->y = s->y * k;
	d->z = s->z * k;
})
DEF2(VECNormalize, (Vec* s, Vec* d), {
	f32 r = 1.0f / sqrtf(s->x * s->x + s->y * s->y + s->z * s->z);
	d->x  = s->x * r;
	d->y  = s->y * r;
	d->z  = s->z * r;
})
extern "C" f32 C_VECSquareMag(Vec* v) { return v->x * v->x + v->y * v->y + v->z * v->z; }
extern "C" f32 PSVECSquareMag(Vec* v) { return C_VECSquareMag(v); }
extern "C" f32 C_VECMag(Vec* v) { return sqrtf(C_VECSquareMag(v)); }
extern "C" f32 PSVECMag(Vec* v) { return C_VECMag(v); }
extern "C" f32 C_VECDotProduct(Vec* a, Vec* b) { return a->x * b->x + a->y * b->y + a->z * b->z; }
extern "C" f32 PSVECDotProduct(Vec* a, Vec* b) { return C_VECDotProduct(a, b); }
DEF2(VECCrossProduct, (Vec* a, Vec* b, Vec* c), {
	Vec t;
	t.x = a->y * b->z - a->z * b->y;
	t.y = a->z * b->x - a->x * b->z;
	t.z = a->x * b->y - a->y * b->x;
	*c  = t;
})
extern "C" f32 C_VECSquareDistance(Vec* a, Vec* b)
{
	f32 x = a->x - b->x, y = a->y - b->y, z = a->z - b->z;
	return x * x + y * y + z * z;
}
extern "C" f32 PSVECSquareDistance(Vec* a, Vec* b) { return C_VECSquareDistance(a, b); }
extern "C" f32 C_VECDistance(Vec* a, Vec* b) { return sqrtf(C_VECSquareDistance(a, b)); }
extern "C" f32 PSVECDistance(Vec* a, Vec* b) { return C_VECDistance(a, b); }

// --- Viewing and projection ---
extern "C" void C_MTXLookAt(Mtx m, Point3dPtr camPos, VecPtr camUp, Point3dPtr target)
{
	Vec look = { camPos->x - target->x, camPos->y - target->y, camPos->z - target->z };
	C_VECNormalize(&look, &look);
	Vec right;
	C_VECCrossProduct(camUp, &look, &right);
	C_VECNormalize(&right, &right);
	Vec up;
	C_VECCrossProduct(&look, &right, &up);
	m[0][0] = right.x;
	m[0][1] = right.y;
	m[0][2] = right.z;
	m[0][3] = -C_VECDotProduct(camPos, &right);
	m[1][0] = up.x;
	m[1][1] = up.y;
	m[1][2] = up.z;
	m[1][3] = -C_VECDotProduct(camPos, &up);
	m[2][0] = look.x;
	m[2][1] = look.y;
	m[2][2] = look.z;
	m[2][3] = -C_VECDotProduct(camPos, &look);
}
extern "C" void C_MTXPerspective(Mtx44 m, f32 fovY, f32 aspect, f32 n, f32 f)
{
	f32 cot = 1.0f / tanf(fovY * 0.5f * (3.14159265358979323846f / 180.0f));
	memset(m, 0, sizeof(Mtx44));
	f32 r   = 1.0f / (f - n);
	m[0][0] = cot / aspect;
	m[1][1] = cot;
	m[2][2] = -n * r;
	m[2][3] = -(f * n) * r;
	m[3][2] = -1.0f;
}
extern "C" void C_MTXFrustum(Mtx44 m, f32 t, f32 b, f32 l, f32 r, f32 n, f32 f)
{
	memset(m, 0, sizeof(Mtx44));
	m[0][0] = 2 * n / (r - l);
	m[0][2] = (r + l) / (r - l);
	m[1][1] = 2 * n / (t - b);
	m[1][2] = (t + b) / (t - b);
	m[2][2] = -n / (f - n);
	m[2][3] = -(f * n) / (f - n);
	m[3][2] = -1.0f;
}
extern "C" void C_MTXOrtho(Mtx44 m, f32 t, f32 b, f32 l, f32 r, f32 n, f32 f)
{
	memset(m, 0, sizeof(Mtx44));
	m[0][0] = 2.0f / (r - l);
	m[0][3] = -(r + l) / (r - l);
	m[1][1] = 2.0f / (t - b);
	m[1][3] = -(t + b) / (t - b);
	m[2][2] = -1.0f / (f - n);
	m[2][3] = -f / (f - n);
	m[3][3] = 1.0f;
}
extern "C" void C_MTXLightPerspective(Mtx m, f32 fovY, f32 aspect, f32 sS, f32 sT, f32 tS, f32 tT)
{
	f32 cot = 1.0f / tanf(fovY * 0.5f * (3.14159265358979323846f / 180.0f));
	memset(m, 0, sizeof(Mtx));
	m[0][0] = cot / aspect * sS;
	m[0][2] = -tS;
	m[1][1] = cot * sT;
	m[1][2] = -tT;
	m[2][2] = -1.0f;
}
extern "C" void C_MTXLightFrustum(Mtx m, f32 t, f32 b, f32 l, f32 r, f32 n, f32 sS, f32 sT, f32 tS, f32 tT)
{
	memset(m, 0, sizeof(Mtx));
	m[0][0] = 2 * n / (r - l) * sS;
	m[0][2] = (r + l) / (r - l) * sS - tS;
	m[1][1] = 2 * n / (t - b) * sT;
	m[1][2] = (t + b) / (t - b) * sT - tT;
	m[2][2] = -1.0f;
}
extern "C" void C_MTXLightOrtho(Mtx m, f32 t, f32 b, f32 l, f32 r, f32 sS, f32 sT, f32 tS, f32 tT)
{
	memset(m, 0, sizeof(Mtx));
	m[0][0] = 2.0f / (r - l) * sS;
	m[0][3] = -(r + l) / (r - l) * sS + tS;
	m[1][1] = 2.0f / (t - b) * sT;
	m[1][3] = -(t + b) / (t - b) * sT + tT;
	m[2][3] = 1.0f;
}
