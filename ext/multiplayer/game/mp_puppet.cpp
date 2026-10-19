// Other players drawn in this game (mp_puppet.hpp).
#include "mp_puppet.hpp"
#include "mp_mirror.hpp"
#include "../mp_recolour.h"

#include <JSystem/J3D/J3DGraphAnimator/J3DMaterialAnm.hpp>
#include <JSystem/J3D/J3DGraphAnimator/J3DModel.hpp>
#include <JSystem/J3D/J3DGraphBase/Components/J3DGXColorS10.hpp>
#include <JSystem/J3D/J3DGraphBase/J3DMaterial.hpp>
#include <JSystem/J3D/J3DGraphBase/J3DTexture.hpp>
#include <JSystem/J3D/J3DGraphBase/J3DDrawBuffer.hpp>
#include <JSystem/J3D/J3DGraphBase/J3DSys.hpp>
#include <JSystem/J3D/J3DGraphLoader/J3DModelLoader.hpp>
#include <JSystem/JKernel/JKRFileLoader.hpp>
#include <JSystem/JKernel/JKRHeap.hpp>
#include <JSystem/JUtility/JUTNameTab.hpp>
#include <M3DUtil/M3UJoint.hpp>
#include <M3DUtil/M3UModelMario.hpp>
#include <M3DUtil/MActor.hpp>
#include <M3DUtil/MActorData.hpp>
#include <M3DUtil/MActorUtil.hpp>
#include <MarioUtil/DrawUtil.hpp>
#include <MarioUtil/LightUtil.hpp>
#include <MarioUtil/MathUtil.hpp>
#include <MarioUtil/PacketUtil.hpp>
#include <MarioUtil/TexUtil.hpp>
#include <MoveBG/MapObjBase.hpp>
#include <MoveBG/MapObjManager.hpp>
#include <Player/Mario.hpp>
#include <Player/MarioDirtyStrings.hpp>
#include <Player/WaterGun.hpp>
#include <Player/Yoshi.hpp>
#include <System/MarDirector.hpp>
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

const u32 kMarioLoadFlags = J3DMLF_MaterialPEFull | (16 << J3DMLF_TevStageNumShift);
const int kMarioAnims     = 199;
const int kMarioTexPatterns = 0x18;

// A copy of a loaded file, so changes to it (a recoloured texture, another
// texture header) leave the original alone. The game's loaders read model
// files in place, and two loads of one file share its texture headers.
void* copyResource(const char* path)
{
	void* res = JKRFileLoader::getGlbResource(path);
	if (!res)
		return nullptr;
	long size = JKRFileLoader::getResSize(res, nullptr);
	if (size <= 0)
		return nullptr;
	void* copy = JKRHeap::alloc((u32)size, 32, nullptr);
	if (!copy)
		return nullptr;
	memcpy(copy, res, (size_t)size);
	return copy;
}

J3DModelData* loadCopy(const char* path, u32 flags)
{
	void* copy = copyResource(path);
	return copy ? J3DModelLoaderDataBase::load(copy, flags) : nullptr;
}

// ---------------------------------------------------------------- recolouring
// Mario's clothes are colours in his texture (H_ma_new_main_s3tc, CMPR), not
// material colours: a character is that texture with its colours changed.
// CMPR keeps two RGB565 colours a 4x4 block and mixes the rest from them, so
// changing the two changes the block.

void unpack565(u16 c, int& r, int& g, int& b)
{
	r = (c >> 11) & 31;
	g = (c >> 5) & 63;
	b = c & 31;
	r = (r << 3) | (r >> 2);
	g = (g << 2) | (g >> 4);
	b = (b << 3) | (b >> 2);
}

u16 pack565(int r, int g, int b)
{
	r = r < 0 ? 0 : r > 255 ? 255 : r;
	g = g < 0 ? 0 : g > 255 ? 255 : g;
	b = b < 0 ? 0 : b > 255 ? 255 : b;
	return (u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

using mp_recolour::recolour;

void recolourTexture(ResTIMG* timg, int character)
{
	if (!timg || character == MPG_CHAR_MARIO || timg->format != GX_TF_CMPR)
		return;
	u8* data          = (u8*)timg + timg->imageDataOffset;
	const u32 blocks  = (u32)timg->width * timg->height / 16;
	for (u32 i = 0; i < blocks; i++) {
		u8* blk     = data + i * 8;
		u16 c0      = (u16)(blk[0] << 8 | blk[1]);
		u16 c1      = (u16)(blk[2] << 8 | blk[3]);
		u32 idx     = (u32)blk[4] << 24 | (u32)blk[5] << 16 | (u32)blk[6] << 8 | blk[7];
		bool four   = c0 > c1; // four colours, or three and transparent
		int r, g, b;
		unpack565(c0, r, g, b);
		recolour(character, r, g, b);
		u16 n0 = pack565(r, g, b);
		unpack565(c1, r, g, b);
		recolour(character, r, g, b);
		u16 n1 = pack565(r, g, b);
		if (four ? n0 < n1 : n0 > n1) {
			// keep the block's mode: swap the colours and the indices naming them
			u16 t = n0;
			n0    = n1;
			n1    = t;
			if (four) {
				idx ^= 0x55555555;
			} else {
				u32 out = 0;
				for (int p = 0; p < 16; p++) {
					u32 v = (idx >> (p * 2)) & 3;
					if (v < 2)
						v ^= 1;
					out |= v << (p * 2);
				}
				idx = out;
			}
		}
		if (four && n0 == n1) {
			// equal colours would read as three-and-transparent
			if (n0 == 0xFFFF)
				n1--;
			else
				n0++;
		}
		blk[0] = (u8)(n0 >> 8);
		blk[1] = (u8)n0;
		blk[2] = (u8)(n1 >> 8);
		blk[3] = (u8)n1;
		blk[4] = (u8)(idx >> 24);
		blk[5] = (u8)(idx >> 16);
		blk[6] = (u8)(idx >> 8);
		blk[7] = (u8)idx;
	}
	DCFlushRange(data, blocks * 8);
}

ResTIMG* mainTexture(J3DModelData* data)
{
	if (!data || !data->getTexture() || data->getTexture()->getNum() == 0)
		return nullptr;
	return data->getTexture()->getResTIMG(0);
}

// No goop: the dirt texture's share (the first konst colour's alpha, which
// TMario::addDirty sets for Mario) is nothing.
void clearDirt(J3DModelData* data)
{
	for (u16 i = 0; i < data->getMaterialNum(); i++)
		if (J3DGXColor* k = data->getMaterialNodePointer(i)->getTevBlock()->getTevKColor(0))
			k->color.a = 0;
}

void setShapes(J3DModelData* data, bool hidden)
{
	for (u16 i = 0; i < data->getShapeNum(); i++) {
		if (hidden)
			data->getShapeNodePointer(i)->onFlag(J3DShpFlag_Visible);
		else
			data->getShapeNodePointer(i)->offFlag(J3DShpFlag_Visible);
	}
}

// Rotation matrix <-> quaternion (x, y, z, w), for turning smoothly.
void toQuat(const f32 (*m)[4], f32 q[4])
{
	const f32 tr = m[0][0] + m[1][1] + m[2][2];
	if (tr > 0.0f) {
		const f32 s = sqrtf(tr + 1.0f) * 2.0f;
		q[3] = 0.25f * s;
		q[0] = (m[2][1] - m[1][2]) / s;
		q[1] = (m[0][2] - m[2][0]) / s;
		q[2] = (m[1][0] - m[0][1]) / s;
	} else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
		const f32 s = sqrtf(1.0f + m[0][0] - m[1][1] - m[2][2]) * 2.0f;
		q[3] = (m[2][1] - m[1][2]) / s;
		q[0] = 0.25f * s;
		q[1] = (m[0][1] + m[1][0]) / s;
		q[2] = (m[0][2] + m[2][0]) / s;
	} else if (m[1][1] > m[2][2]) {
		const f32 s = sqrtf(1.0f + m[1][1] - m[0][0] - m[2][2]) * 2.0f;
		q[3] = (m[0][2] - m[2][0]) / s;
		q[0] = (m[0][1] + m[1][0]) / s;
		q[1] = 0.25f * s;
		q[2] = (m[1][2] + m[2][1]) / s;
	} else {
		const f32 s = sqrtf(1.0f + m[2][2] - m[0][0] - m[1][1]) * 2.0f;
		q[3] = (m[1][0] - m[0][1]) / s;
		q[0] = (m[0][2] + m[2][0]) / s;
		q[1] = (m[1][2] + m[2][1]) / s;
		q[2] = 0.25f * s;
	}
}

void fromQuat(const f32 q[4], f32 (*m)[4])
{
	const f32 x = q[0], y = q[1], z = q[2], w = q[3];
	m[0][0] = 1 - 2 * (y * y + z * z), m[0][1] = 2 * (x * y - z * w), m[0][2] = 2 * (x * z + y * w);
	m[1][0] = 2 * (x * y + z * w), m[1][1] = 1 - 2 * (x * x + z * z), m[1][2] = 2 * (y * z - x * w);
	m[2][0] = 2 * (x * z - y * w), m[2][1] = 2 * (y * z + x * w), m[2][2] = 1 - 2 * (x * x + y * y);
}

// Their scale (the matrix's columns' lengths), kept apart from the turn.
f32 columnLength(const f32 (*m)[4], int c) { return sqrtf(m[0][c] * m[0][c] + m[1][c] * m[1][c] + m[2][c] * m[2][c]); }

void blendMatrix(const f32 (*a)[4], const f32 (*b)[4], f32 t, f32 (*out)[4])
{
	f32 sa[3], sb[3], ra[3][4], rb[3][4];
	for (int c = 0; c < 3; c++) {
		sa[c] = columnLength(a, c), sb[c] = columnLength(b, c);
		for (int r = 0; r < 3; r++) {
			ra[r][c] = sa[c] > 1e-6f ? a[r][c] / sa[c] : 0.0f;
			rb[r][c] = sb[c] > 1e-6f ? b[r][c] / sb[c] : 0.0f;
		}
	}
	f32 qa[4], qb[4], q[4];
	toQuat(ra, qa);
	toQuat(rb, qb);
	f32 dot = qa[0] * qb[0] + qa[1] * qb[1] + qa[2] * qb[2] + qa[3] * qb[3];
	if (dot < 0.0f) { // the short way round
		for (int i = 0; i < 4; i++)
			qb[i] = -qb[i];
		dot = -dot;
	}
	f32 wa = 1.0f - t, wb = t;
	if (dot < 0.9995f) {
		const f32 angle = acosf(dot), sine = sinf(angle);
		wa = sinf((1.0f - t) * angle) / sine;
		wb = sinf(t * angle) / sine;
	}
	f32 len = 0.0f;
	for (int i = 0; i < 4; i++) {
		q[i] = qa[i] * wa + qb[i] * wb;
		len += q[i] * q[i];
	}
	len = sqrtf(len);
	for (int i = 0; i < 4; i++)
		q[i] /= len > 1e-6f ? len : 1.0f;
	fromQuat(q, out);
	for (int c = 0; c < 3; c++) {
		const f32 scale = sa[c] + (sb[c] - sa[c]) * t;
		for (int r = 0; r < 3; r++)
			out[r][c] *= scale;
	}
	for (int r = 0; r < 3; r++)
		out[r][3] = a[r][3] + (b[r][3] - a[r][3]) * t;
}

// A frame between two of the same looping animation: forwards, over the
// loop's end when the later one is smaller.
f32 blendFrame(f32 a, f32 b, f32 t, f32 length)
{
	if (b >= a || length <= 0.0f)
		return a + (b - a) * t;
	f32 f = a + (b + length - a) * t;
	return f >= length ? f - length : f;
}

} // namespace

void TMpPuppet::blendPoses(const MpgPose& a, const MpgPose& b, f32 t, MpgPose* out)
{
	// what is not in between (a nozzle, flags, which animation) is the
	// nearer pose's; the water is always the newer pose's
	*out = t < 0.5f ? a : b;
	out->flags    = (a.flags & ~MPG_POSE_SPRAYING) | (b.flags & MPG_POSE_SPRAYING);
	out->emitters = b.emitters;
	memcpy(out->emitPos, b.emitPos, sizeof out->emitPos);
	memcpy(out->emitDir, b.emitDir, sizeof out->emitDir);
	memcpy(out->emitSpeed, b.emitSpeed, sizeof out->emitSpeed);
	out->pressure = b.pressure;
	if (t <= 0.0f)
		return;
	// a jump between poses (a warp, a respawn) is not crossed smoothly
	const f32 dx = b.mtx[0][3] - a.mtx[0][3], dy = b.mtx[1][3] - a.mtx[1][3], dz = b.mtx[2][3] - a.mtx[2][3];
	if (dx * dx + dy * dy + dz * dz > 800.0f * 800.0f)
		return;
	blendMatrix(a.mtx, b.mtx, t, out->mtx);
	M3UModelMario* pm = mModel;
	if (a.bodyBck == b.bodyBck && a.bodyBck < kMarioAnims && pm->unk4->unk4[a.bodyBck]) {
		out->bodyFrame = blendFrame(a.bodyFrame, b.bodyFrame, t, pm->unk4->unk4[a.bodyBck]->getFrameMax());
		out->bodyBlend = a.bodyBlend + (b.bodyBlend - a.bodyBlend) * t;
	}
	if (a.upperBck == b.upperBck && a.upperBck < kMarioAnims && pm->unk4->unk4[a.upperBck]
	    && (a.flags & b.flags & MPG_POSE_UPPER))
		out->upperFrame = blendFrame(a.upperFrame, b.upperFrame, t, pm->unk4->unk4[a.upperBck]->getFrameMax());
	if (a.btp == b.btp && a.btp < kMarioTexPatterns && pm->unk4->unk8[a.btp])
		out->btpFrame = blendFrame(a.btpFrame, b.btpFrame, t, pm->unk4->unk8[a.btp]->getFrameMax());
	if (a.fluddBck == b.fluddBck && b.fluddFrame >= a.fluddFrame)
		out->fluddFrame = a.fluddFrame + (b.fluddFrame - a.fluddFrame) * t;
	if (a.yoshiBck == b.yoshiBck && b.yoshiFrame >= a.yoshiFrame)
		out->yoshiFrame = a.yoshiFrame + (b.yoshiFrame - a.yoshiFrame) * t;
}

TMpPuppet::TMpPuppet(int slot, bool self)
    : THitActor("<MpPuppet>")
    , mSlot(slot)
    , mCharacter(mpg_character(slot))
    , mSelf(self)
    , mActive(false)
    , mHaveSmoothed(false)
    , mLastSession(0)
    , mWaterCarry(0.0f)
    , mSpawnFrames(0)
    , mGoneFrames(1000)
    , mModel(nullptr)
    , mBodyData(nullptr)
    , mFludd(nullptr)
    , mFluddData(nullptr)
    , mNozzleJoint(0)
    , mYoshi(nullptr)
    , mSurf(nullptr)
    , mRide(nullptr)
    , mOpa(nullptr)
    , mXlu(nullptr)
    , mSwapped(false)
    , mDressedVersion(0xFFFFFFFFu)
    , mDrawn(false)
{
	memset(mView, 0, sizeof mView);
	memset(mProjection, 0, sizeof mProjection);
	memset(&mPose, 0, sizeof mPose);
	memset(mHands, 0, sizeof mHands);
	memset(mCap, 0, sizeof mCap);
	memset(mNozzles, 0, sizeof mNozzles);
	memset(mSurfColor, 0, sizeof mSurfColor);
	mSaved[0] = mSaved[1] = nullptr;
	mSmoothed[0] = mSmoothed[1] = mSmoothed[2] = 0.0f;
	mHead[0] = mHead[1] = mHead[2] = 0.0f;
	// seen by nothing: it is a picture of a player whose game does the hitting
	mActorType = 0;
	onHitFlag(HIT_FLAG_NO_COLLISION);
}

bool TMpPuppet::buildBody()
{
	TMario* orig = gpMarioOriginal;
	if (!orig || !orig->getM3UModel())
		return false;
	mBodyData = loadCopy("/mario/bmd/ma_mdl1.bmd", kMarioLoadFlags);
	if (!mBodyData)
		return false;
	mJointChest = mBodyData->getJointName()->getIndex("jnt_chest");
	mJointHandR = mBodyData->getJointName()->getIndex("jnt_hand_R");
	mJointHandL = mBodyData->getJointName()->getIndex("jnt_hand_L");
	mJointHead  = mBodyData->getJointName()->getIndex("jnt_head");
	mJointMHead = mBodyData->getJointName()->getIndex("M_head");

	ResTIMG* pollution = (ResTIMG*)JKRFileLoader::getGlbResource(cDirtyFileName);
	if (pollution)
		SMS_ChangeTextureAll(mBodyData, cDirtyTexName, *pollution);
	recolourTexture(mainTexture(mBodyData), mCharacter);
	J3DModel* body = new J3DModel(mBodyData, 0, 1);

	// hands and cap: their own copies, wearing the body's texture
	static const char* const handFiles[2][2] = {
		{"/mario/bmd/ma_hnd2r.bmd", "/mario/bmd/ma_hnd2l.bmd"},
		{"/mario/bmd/ma_hnd3r.bmd", "/mario/bmd/ma_hnd3l.bmd"},
	};
	for (int h = 0; h < 2; h++)
		for (int s = 0; s < 2; s++) {
			J3DModelData* data = loadCopy(handFiles[h][s], kMarioLoadFlags);
			if (!data)
				return false;
			data->getTexture()->setResTIMG(0, *mainTexture(mBodyData));
			DCFlushRange(data->getTexture()->getResTIMG(0), sizeof(ResTIMG));
			if (pollution)
				SMS_ChangeTextureAll(data, cDirtyTexName, *pollution);
			clearDirt(data);
			mHands[h][s] = new J3DModel(data, kMarioLoadFlags, 1);
		}
	static const char* const capFiles[2] = {"/mario/bmd/ma_cap1.bmd", "/mario/bmd/ma_cap3.bmd"};
	for (int c = 0; c < 2; c++) {
		J3DModelData* data = loadCopy(capFiles[c], kMarioLoadFlags);
		if (!data)
			return false;
		data->getTexture()->setResTIMG(0, *mainTexture(mBodyData));
		DCFlushRange(data->getTexture()->getResTIMG(0), sizeof(ResTIMG));
		if (pollution)
			SMS_ChangeTextureAll(data, cDirtyTexName, *pollution);
		clearDirt(data);
		mCap[c] = new J3DModel(data, 0, 1);
		SMS_MakeDLAndLock(mCap[c]);
	}

	// the eyes blink and look by texture pattern, as Mario's do
	u16 eyeL = mBodyData->getMaterialName()->getIndex("_mat_eye_L");
	u16 eyeR = mBodyData->getMaterialName()->getIndex("_mat_eye_R");
	mBodyData->getMaterialNodePointer(eyeL)->change();
	mBodyData->getMaterialNodePointer(eyeL)->setMaterialAnm(new J3DMaterialAnm());
	mBodyData->getMaterialNodePointer(eyeR)->change();
	mBodyData->getMaterialNodePointer(eyeR)->setMaterialAnm(new J3DMaterialAnm());

	// the animation machinery of Shadow Mario's (TEnemyMario::initModel):
	// Mario's own animations, this body's own blend and frame state
	M3UModelCommon* origCommon = orig->getM3UModel()->unk4;
	M3UMtxCalcSIAnmBlendQuat* blend = new M3UMtxCalcSIAnmBlendQuat[2];
	blend[0].mMotionBlendRatio      = 0.0f;
	blend[1].mMotionBlendRatio      = 0.0f;
	J3DFrameCtrl* frames            = new J3DFrameCtrl[3];
	M3UModelCommonMario* common     = new M3UModelCommonMario;
	common->unk4                    = origCommon->unk4;
	common->unk8                    = origCommon->unk8;
	common->unkC                    = origCommon->unkC;
	common->unk18                   = blend;
	M3UModelMario* model            = new M3UModelMario;
	model->unk8                     = body;
	model->unk4                     = common;
	model->unk20                    = common;
	model->unkC                     = frames;
	SomeModelMarioStruct* setInfo   = new SomeModelMarioStruct[2];
	SomeModelMarioStruct first      = {0, 2, 0, {0x14, 0x41}, 0};
	SomeModelMarioStruct upper      = {(s16)mJointChest, 2, 1, {0, 0}, 1};
	setInfo[0]                      = first;
	setInfo[1]                      = upper;
	model->unk10                    = 2;
	model->unk24                    = setInfo;
	M3UModel::Unk1CStruct* texSlot  = new M3UModel::Unk1CStruct;
	texSlot->unk0                   = 0;
	texSlot->unk1                   = 2;
	model->unk1C                    = texSlot;
	model->changeMtxCalcSIAnmBQAnmTransform(0, 0, 0x3e);
	model->changeMtxCalcSIAnmBQAnmTransform(1, 0, 0x41);
	frames[0].setRate(0.0f);
	frames[1].setRate(0.0f);
	frames[2].setRate(0.0f);
	setInfo[1].unk3 = 0xff;
	mModel          = model;

	// as Mario's finalDrawInitialize: the hidden extra shape, his bare
	// hands, display lists, and no goop on him (the dirt colour's alpha)
	mBodyData->getShapeNodePointer(4)->onFlag(J3DShpFlag_Visible);
	showHands(0);
	clearDirt(mBodyData);
	SMS_MakeDLAndLock(body);
	for (u16 i = 0; i < mBodyData->getMaterialNum(); i++)
		if (i == eyeL || i == eyeR)
			body->getMatPacketArray()[i].offFlag(0x1);
	for (u16 i = 0; i < mBodyData->getMaterialNum(); i++)
		SMS_InitPacket_OneTevKColorAndFog(body, i, GX_KCOLOR0, nullptr);
	return true;
}

bool TMpPuppet::buildFludd()
{
	// FLUDD as Mario wears it: his loaded animations, our own models (their
	// pose is ours). Each puppet loads its own copy of FLUDD's files, as of
	// Mario's, so it can wear its player's FLUDD colours (dress) without
	// painting anyone else's; its nozzles wear its pack's texture, as
	// TWaterGun::init gives Mario's his. Without room for the copies, it
	// draws the shared model data (and keeps FLUDD's usual colours).
	TWaterGun* gun = gpMarioOriginal ? gpMarioOriginal->mWaterGun : nullptr;
	if (!gun || !gun->mFluddModel)
		return false;
	const u32 flags = J3DMLF_MaterialPEFull | (4 << J3DMLF_TevStageNumShift);
	mFluddData      = loadCopy("/mario/watergun2/body/wg_mdl1.bmd", flags);
	ResTIMG* packTexture = mFluddData && mFluddData->getTexture() && mFluddData->getTexture()->getNum() > 1
	                         ? mFluddData->getTexture()->getResTIMG(1)
	                         : nullptr;
	mFludd = new MActor(gun->mFluddModel->mAnmData);
	mFludd->setModel(new J3DModel(packTexture ? mFluddData : gun->mFluddModel->getModel()->getModelData(), 0, 1), 0);
	mNozzleJoint = gun->unk1CD8;
	for (int i = 0; i < 6; i++) {
		TNozzleBase* nozzle = gun->mNozzleList[i];
		MActor* actor       = nozzle ? nozzle->getMActor() : nullptr;
		if (!actor)
			continue;
		J3DModelData* data = packTexture ? loadCopy(nozzleBmdData.getBmdPath(i), flags) : nullptr;
		if (data)
			SMS_ChangeTextureAll(data, "H_watergun_main_dummy", *packTexture);
		mNozzles[i] = new MActor(actor->mAnmData);
		mNozzles[i]->setModel(new J3DModel(data ? data : actor->getModel()->getModelData(), 0, 1), 0);
	}
	if (!packTexture)
		mFluddData = nullptr;
	return true;
}

bool TMpPuppet::buildYoshi()
{
	// Yoshi's own model data (his colour is a material colour), Mario's
	// Yoshi's animations
	TYoshi* yoshi = gpMarioOriginal ? gpMarioOriginal->mYoshi : nullptr;
	void* res     = JKRFileLoader::getGlbResource("/yoshi/yoshi_model.bmd");
	if (!yoshi || !yoshi->unk30 || !res)
		return false;
	// one model data for every puppet's Yoshi in this stage (each sets his
	// colour just before he is entered)
	static J3DModelData* shared;
	static unsigned sharedFor;
	if (!shared || sharedFor != MpStageNumber()) {
		shared    = J3DModelLoaderDataBase::load(res, J3DMLF_MaterialPEFull | (4 << J3DMLF_TevStageNumShift));
		sharedFor = MpStageNumber();
	}
	mYoshi = new MActor(yoshi->unk30);
	mYoshi->setModel(new J3DModel(shared, 0, 1), 0);
	mYoshi->initNormalMotionBlend();
	mYoshi->offMakeDL();
	return true;
}

// The Blooper a player surfs on: the game draws Mario's from the stage's
// own three (TMapObjManager, one a colour, TMario::getGesso); each puppet
// has its own, coloured as the one its player rides.
bool TMpPuppet::buildSurf()
{
	if (!gpMapObjManager || !gpMapObjManager->mSurfGessoModelData)
		return false;
	mSurf = SMS_MakeMActorFromSDLModelData(gpMapObjManager->mSurfGessoModelData, gpMapObjManager->getMActorAnmData(), 3);
	if (!mSurf)
		return false;
	memcpy(mSurfColor, &gpMapObjManager->unkA8, sizeof mSurfColor);
	TMapObjBase::initPacketMatColor(mSurf->mModel, GX_TEVREG1, (const GXColorS10*)mSurfColor);
	mSurf->setBck("surfgeso_run1");
	if (J3DFrameCtrl* fc = mSurf->getFrameCtrl(ANM_TYPE_BCK))
		fc->setRate(0.5f);
	return true;
}

bool TMpPuppet::init()
{
	JKRHeap* heap = JKRHeap::getCurrentHeap();
	s32 free0     = heap->getTotalFreeSize();
	if (!buildBody())
		return false;
	s32 free1 = heap->getTotalFreeSize();
	buildFludd();
	s32 free2 = heap->getTotalFreeSize();
	buildYoshi();
	buildSurf();
	s32 free3 = heap->getTotalFreeSize();
	mOpa = new J3DDrawBuffer(0x20);
	mXlu = new J3DDrawBuffer(0x20);
	fprintf(stderr, "[multiplayer] %s: body %d, FLUDD %d, Yoshi %d bytes\n", mpg_character_name(mSlot),
	        (int)(free0 - free1), (int)(free1 - free2), (int)(free2 - free3));
	return true;
}

// Each texture of theirs this game has a picture for, in their colours: the
// body's (its cap and hands draw the body's texture data too) and FLUDD's
// (its nozzles draw its pack's).
void TMpPuppet::dress()
{
	mDressedVersion = mpg_looks_version();
	int dressed = 0, total = 0;
	J3DModelData* const models[2] = {mBodyData, mFluddData};
	for (J3DModelData* model : models) {
		J3DTexture* textures = model ? model->getTexture() : nullptr;
		JUTNameTab* names    = model ? model->getTextureName() : nullptr;
		if (!textures || !names)
			continue;
		for (u16 i = 0; i < textures->getNum(); i++, total++) {
			ResTIMG* timg    = textures->getResTIMG(i);
			const char* name = names->getName(i);
			if (!timg || !name)
				continue;
			const void* data = (const u8*)timg + timg->imageDataOffset;
			const u32 bytes  = GXGetTexBufferSize(timg->width, timg->height, timg->format, GX_FALSE, 0);
			dressed += mpg_dress(mSlot, mCharacter, name, data, bytes);
		}
	}
	static const bool debug = getenv("SMS_MULTIPLAYER_DEBUG") != nullptr;
	if (debug)
		fprintf(stderr, "[multiplayer] %s dressed: %d of %d textures\n", mpg_character_name(mSlot), dressed, total);
}

bool TMpPuppet::nameSpot(const f32* viewer, f32* x, f32* y, f32* alpha) const
{
	// near enough: the full name inside kNear, fading out to nothing at kFar
	const f32 kNear = 900.0f, kFar = 1300.0f;
	if (!mActive || !mDrawn || mProjection[0] != 0.0f) // perspective only
		return false;
	const f32 dx = mPosition.x - viewer[0], dy = mPosition.y - viewer[1], dz = mPosition.z - viewer[2];
	const f32 d = sqrtf(dx * dx + dy * dy + dz * dz);
	if (d >= kFar)
		return false;
	*alpha = d <= kNear ? 1.0f : (kFar - d) / (kFar - kNear);
	// above the cap
	const f32 at[3] = {mHead[0], mHead[1] + 85.0f, mHead[2]};
	f32 v[3];
	for (int r = 0; r < 3; r++)
		v[r] = mView[r][0] * at[0] + mView[r][1] * at[1] + mView[r][2] * at[2] + mView[r][3];
	if (v[2] > -1.0f) // behind the camera
		return false;
	const f32 w = -v[2];
	const f32 nx = (mProjection[1] * v[0] + mProjection[2] * v[2]) / w;
	const f32 ny = (mProjection[3] * v[1] + mProjection[4] * v[2]) / w;
	*x = (nx + 1.0f) * 0.5f;
	*y = (1.0f - ny) * 0.5f;
	return *x > -0.1f && *x < 1.1f && *y > -0.1f && *y < 1.1f;
}

void TMpPuppet::showHands(int hand)
{
	J3DModelData* body = mBodyData;
	// J3DShpFlag_Visible set means hidden
	if (hand == 0) {
		body->getShapeNodePointer(5)->offFlag(J3DShpFlag_Visible);
		body->getShapeNodePointer(6)->offFlag(J3DShpFlag_Visible);
	} else {
		body->getShapeNodePointer(5)->onFlag(J3DShpFlag_Visible);
		body->getShapeNodePointer(6)->onFlag(J3DShpFlag_Visible);
	}
	for (int h = 0; h < 2; h++)
		for (int s = 0; s < 2; s++)
			if (mHands[h][s])
				setShapes(mHands[h][s]->getModelData(), hand != h + 1);
}

void TMpPuppet::applyPose(JDrama::TGraphics* graphics)
{
	M3UModelMario* pm      = mModel;
	SomeModelMarioStruct* s = pm->unk24;
	const MpgPose& p       = mPose;

	u16 body = p.bodyBck < kMarioAnims ? p.bodyBck : 0x3e;
	if (s[0].unk4[0] != body)
		pm->changeMtxCalcSIAnmBQAnmTransform(0, 0, body);
	pm->getFrameCtrl(0).setRate(0.0f);
	pm->getFrameCtrl(0).setFrame(p.bodyFrame);
	pm->unk20->unk18[0].mMotionBlendRatio = p.bodyBlend;
	if ((p.flags & MPG_POSE_UPPER) && p.upperBck < kMarioAnims) {
		s[1].unk3 = 1;
		if (s[1].unk4[0] != p.upperBck)
			pm->changeMtxCalcSIAnmBQAnmTransform(1, 0, p.upperBck);
		pm->getFrameCtrl(1).setFrame(p.upperFrame);
	} else {
		s[1].unk3 = 0xff;
	}
	if (p.btp < kMarioTexPatterns) {
		if (pm->unk1C->unk0 != p.btp)
			pm->changeAnmTexPattern(0, p.btp);
		pm->getFrameCtrl(2).setFrame(p.btpFrame);
	}

	// where: the root matrix as blended from their poses
	Mtx base;
	memcpy(base, p.mtx, sizeof base);
	mPosition.x = base[0][3];
	mPosition.y = base[1][3];
	mPosition.z = base[2][3];
	// a player who just arrived grows in, overshooting a little, rather than
	// popping up from nothing
	if (!mSelf && mSpawnFrames < 14) {
		const f32 t     = (f32)(mSpawnFrames + 1) / 14.0f;
		const f32 scale = t < 0.7f ? t / 0.7f * 1.12f : 1.12f - (t - 0.7f) / 0.3f * 0.12f;
		for (int r = 0; r < 3; r++)
			for (int c = 0; c < 3; c++)
				base[r][c] *= scale;
	}
	MTXCopy(base, pm->getModel()->getBaseTRMtx());
	pm->perform(CUE_CALC_ANIM, graphics);

	J3DModel* bodyModel = pm->getModel();
	MtxPtr head         = bodyModel->getAnmMtx(mJointHead);
	mHead[0]            = head[0][3];
	mHead[1]            = head[1][3];
	mHead[2]            = head[2][3];

	showHands(p.hand);
	// the body's shapes as the player's game shows them (the shirt, the
	// hands in the body, the hair under the cap)
	for (u16 i = 0; i < mBodyData->getShapeNum() && i < 32; i++) {
		if (p.hiddenShapes & (1u << i))
			mBodyData->getShapeNodePointer(i)->onFlag(J3DShpFlag_Visible);
		else
			mBodyData->getShapeNodePointer(i)->offFlag(J3DShpFlag_Visible);
	}
	for (int h = 0; h < 2; h++) {
		mHands[h][0]->setBaseTRMtx(bodyModel->getAnmMtx(mJointHandR));
		mHands[h][1]->setBaseTRMtx(bodyModel->getAnmMtx(mJointHandL));
		mHands[h][0]->calc();
		mHands[h][1]->calc();
	}
	J3DModel* cap = mCap[(p.flags & MPG_POSE_CAP) ? 0 : 1];
	cap->setBaseTRMtx(bodyModel->getAnmMtx(mJointMHead));
	cap->calc();

	if (mFludd && (p.flags & MPG_POSE_FLUDD)) {
		// on the back, tilted as TWaterGun::setBaseTRMtx tilts it
		Mtx chest;
		MTXCopy(bodyModel->getAnmMtx(mJointChest), chest);
		f32 lean     = chest[1][0] < 0.0f ? -chest[1][0] : chest[1][0];
		s16 angle    = (s16)(lean * (0 - -0x1800) + -0x1800);
		Mtx roll, gun;
		MsMtxSetRotRPH(roll, 0.0f, 0.0f, angle * (360.0f / 65536.0f));
		MTXConcat(chest, roll, gun);
		mFludd->getModel()->setBaseTRMtx(gun);
		if (mFludd->getCurAnmIdx(ANM_TYPE_BCK) != p.fluddBck)
			mFludd->setBckFromIndex(p.fluddBck);
		if (J3DFrameCtrl* fc = mFludd->getFrameCtrl(ANM_TYPE_BCK)) {
			fc->setRate(0.0f);
			fc->setFrame(p.fluddFrame);
		}
		mFludd->perform(CUE_CALC_ANIM, graphics);
		MActor* nozzle = p.nozzle < 6 ? mNozzles[p.nozzle] : nullptr;
		if (nozzle) {
			nozzle->getModel()->setBaseTRMtx(mFludd->getModel()->getAnmMtx(mNozzleJoint));
			nozzle->perform(CUE_CALC_ANIM, graphics);
		}
	}

	if (mYoshi && (p.flags & MPG_POSE_YOSHI)) {
		// Mario rides at Yoshi's root (TYoshi::calcAnim: getTakenMtx)
		mYoshi->getModel()->setBaseTRMtx(base);
		if (mYoshi->getCurAnmIdx(ANM_TYPE_BCK) != p.yoshiBck)
			mYoshi->setBckFromIndex(p.yoshiBck);
		if (J3DFrameCtrl* fc = mYoshi->getFrameCtrl(ANM_TYPE_BCK)) {
			fc->setRate(0.0f);
			fc->setFrame(p.yoshiFrame);
		}
		mYoshi->perform(CUE_CALC_ANIM, graphics);
	}

	if (mSurf && (p.more & MPG_POSE2_SURF)) {
		// under them, at their root (TMario::calcAnim), in its rider's colour
		const GXColorS10* colours[3] = {&gpMapObjManager->unkA8, &gpMapObjManager->unkB0, &gpMapObjManager->unkB8};
		const int type               = (p.more & MPG_POSE2_SURF_TYPE) >> 2;
		memcpy(mSurfColor, colours[type < 3 ? type : 0], sizeof mSurfColor);
		mSurf->getModel()->setBaseTRMtx(base);
		mSurf->perform(CUE_CALC_ANIM, graphics);
	}

	// their own boat (each player has theirs): under them, as their game has it
	const f32 root[3] = {base[0][3], base[1][3], base[2][3]};
	mRide             = MpMirrorRide(mSlot, root);
	if (mRide)
		mRide->perform(CUE_CALC_ANIM, graphics);
}

// Debug: how smoothly another player is drawn while they move. Each frame's
// step is held against the steps just before: a stall (barely moving) or a
// jump (far more than lately) is a hitch the eye catches.
void MpMeasureSmoothness(int slot, const MpgPose& pose)
{
	static const bool debug = getenv("SMS_MULTIPLAYER_DEBUG") != nullptr;
	if (!debug || slot < 0 || slot >= MPG_MAX_PLAYERS)
		return;
	struct Track {
		f32 x, z, avg;
		bool have;
		int moving, stalls, jumps, frames;
	};
	static Track tracks[MPG_MAX_PLAYERS];
	Track& k     = tracks[slot];
	const f32 x  = pose.mtx[0][3], z = pose.mtx[2][3];
	if (k.have) {
		const f32 step = sqrtf((x - k.x) * (x - k.x) + (z - k.z) * (z - k.z));
		if (k.avg > 3.0f && step < 400.0f) { // moving (not standing, not a warp)
			k.moving++;
			if (step < k.avg * 0.25f)
				k.stalls++;
			else if (step > k.avg * 2.5f)
				k.jumps++;
		}
		k.avg += (step - k.avg) * 0.2f;
	}
	k.x = x, k.z = z, k.have = true;
	if (++k.frames % 600 == 0 && k.moving) {
		fprintf(stderr, "[multiplayer] drawn smoothness, %s: %d moving frames, %d stalls, %d jumps\n",
		        mpg_character_name(slot), k.moving, k.stalls, k.jumps);
		k.moving = k.stalls = k.jumps = 0;
	}
}

void TMpPuppet::perform(u32 cue, JDrama::TGraphics* graphics)
{
	if (!mModel)
		return;

	if (cue & CUE_CALC_ANIM) {
		float age = 0.0f;
		MpgPose pose;
		if (mSelf) {
			// Mario as he is this frame (his calcAnim ran in the movement pass)
			MpCapturePose(gpMarioOriginal, &pose);
			mHaveSmoothed = false;
			mActive       = gpMarioOriginal && (pose.flags & MPG_POSE_VISIBLE);
		} else {
			// between the two poses either side of a moment just past
			MpgPose a, b;
			f32 t = 0.0f;
			mActive = mpg_pose_pair(mSlot, &a, &b, &t) && (b.flags & MPG_POSE_VISIBLE);
			if (mActive)
				blendPoses(a, b, t, &pose);
			if (mActive)
				MpMeasureSmoothness(mSlot, pose);
			static const bool trace = getenv("SMS_MULTIPLAYER_TRACE") != nullptr;
			MpgPose newest;
			if (trace && mActive && mpg_pose(mSlot, &newest, &age))
				fprintf(stderr, "[mptrace] %d drawn %.1f %.1f newest %.1f %.1f frame %.2f t %.2f\n", mSlot,
				        pose.mtx[0][3], pose.mtx[2][3], newest.mtx[0][3], newest.mtx[2][3], pose.bodyFrame, t);
		}
		if (!mActive && mGoneFrames < 1000)
			mGoneFrames++;
		if (!mActive)
			mDrawn = false;
		// in their colours once they are here (not for empty slots)
		if (mActive && mDressedVersion != mpg_looks_version())
			dress();
		if (mActive) {
			// gone a second or more, or in a new stage: they appear again
			if (mGoneFrames > 30 || pose.session != mLastSession)
				mSpawnFrames = 0;
			else if (mSpawnFrames < 1000)
				mSpawnFrames++;
			mGoneFrames = 0;
			if (pose.session != mLastSession)
				mHaveSmoothed = false;
			mLastSession = pose.session;
			mPose        = pose;
			applyPose(graphics);
			// their water (our own player's is the game's own)
			if (!mSelf && (pose.flags & MPG_POSE_SPRAYING))
				MpEmitWater(pose, &mWaterCarry);
		}
	}
	if (!mActive && !mSwapped)
		return;

	if (cue & CUE_CALC_VIEW) {
		MTXCopy(graphics->mViewMtx, j3dSys.mViewMtx);
		MTXCopy(graphics->mViewMtx, mView);
		mModel->getModel()->viewCalc();
		for (int h = 0; h < 2; h++)
			for (int s = 0; s < 2; s++)
				mHands[h][s]->viewCalc();
		mCap[(mPose.flags & MPG_POSE_CAP) ? 0 : 1]->viewCalc();
		if (mFludd && (mPose.flags & MPG_POSE_FLUDD)) {
			mFludd->perform(CUE_CALC_VIEW, graphics);
			if (mPose.nozzle < 6 && mNozzles[mPose.nozzle])
				mNozzles[mPose.nozzle]->perform(CUE_CALC_VIEW, graphics);
		}
		if (mYoshi && (mPose.flags & MPG_POSE_YOSHI))
			mYoshi->perform(CUE_CALC_VIEW, graphics);
		if (mSurf && (mPose.more & MPG_POSE2_SURF))
			mSurf->perform(CUE_CALC_VIEW, graphics);
		if (mRide)
			mRide->perform(CUE_CALC_VIEW, graphics);
	}
	if (cue & CUE_UNK10000000) {
		mOpa->frameInit();
		mXlu->frameInit();
		mSaved[0]             = j3dSys.mDrawBuffer[0];
		mSaved[1]             = j3dSys.mDrawBuffer[1];
		j3dSys.mDrawBuffer[0] = mOpa;
		j3dSys.mDrawBuffer[1] = mXlu;
		mSwapped              = true;
	}
	if ((cue & CUE_ENTRY) && mActive) {
		mModel->perform(CUE_ENTRY, graphics);
		for (int h = 0; h < 2; h++)
			for (int s = 0; s < 2; s++)
				mHands[h][s]->entry();
		mCap[(mPose.flags & MPG_POSE_CAP) ? 0 : 1]->entry();
		if (mFludd && (mPose.flags & MPG_POSE_FLUDD)) {
			mFludd->perform(CUE_ENTRY, graphics);
			if (mPose.nozzle < 6 && mNozzles[mPose.nozzle])
				mNozzles[mPose.nozzle]->perform(CUE_ENTRY, graphics);
		}
		if (mYoshi && (mPose.flags & MPG_POSE_YOSHI)) {
			// his colour, as TYoshi::entry sets it
			J3DModelData* data = mYoshi->getModel()->getModelData();
			for (u16 i = 0; i < data->getMaterialNum(); i++) {
				J3DGXColorS10 c;
				c.color.r = mPose.yoshiColor[0];
				c.color.g = mPose.yoshiColor[1];
				c.color.b = mPose.yoshiColor[2];
				c.color.a = 255;
				data->getMaterialNodePointer(i)->getTevBlock()->setTevColor(2, &c);
			}
			mYoshi->perform(CUE_ENTRY, graphics);
		}
		if (mSurf && (mPose.more & MPG_POSE2_SURF)) {
			const JGeometry::TVec3<f32> at(mPosition.x, mPosition.y, mPosition.z);
			mSurf->setLightData(nullptr, at);
			mSurf->perform(CUE_ENTRY, graphics);
		}
		if (mRide) {
			const JGeometry::TVec3<f32> at(mPosition.x, mPosition.y, mPosition.z);
			mRide->setLightData(nullptr, at);
			mRide->perform(CUE_ENTRY, graphics);
		}
	}
	if ((cue & CUE_UNK8000000) && mSwapped) {
		j3dSys.mDrawBuffer[0] = mSaved[0];
		j3dSys.mDrawBuffer[1] = mSaved[1];
		mSwapped              = false;
	}
	// drawn under Mario's light, after him (the group draws him first, and
	// his light is set again before the next pass)
	if ((cue & CUE_UNK40000000) && mActive) {
		// the scene's projection, for the name over the head
		GXGetProjectionv(mProjection);
		mDrawn = true;
		if (gpLightManager && gpLightManager->mMarioLight)
			gpLightManager->mMarioLight->setLight(graphics, gpMarioOriginal ? gpMarioOriginal->mLightID : 0);
		j3dSys.setUnk4C(3);
		mOpa->draw();
	}
	if ((cue & CUE_UNK20000000) && mActive) {
		if (gpLightManager && gpLightManager->mMarioLight)
			gpLightManager->mMarioLight->setLight(graphics, gpMarioOriginal ? gpMarioOriginal->mLightID : 0);
		j3dSys.setUnk4C(4);
		mXlu->draw();
	}
}
