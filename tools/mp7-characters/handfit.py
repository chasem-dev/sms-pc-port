"""Rotation (about the wrist) that best fits the character's hand onto Mario's, by ICP."""
import sys, os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np, bmdtris

def points(d, shapes):
    T = bmdtris.triangles(d, [np.eye(4)]*64, shapes)
    P = np.array([t[1] for t in T]).reshape(-1, 3)
    # area-weighted surface samples
    out = []
    for t in T:
        a, b, c = t[1]
        for u, v in ((1/3, 1/3), (0.6, 0.2), (0.2, 0.6), (0.2, 0.2)):
            out.append(a + u*(b-a) + v*(c-a))
    return np.array(out)

def rot(axis, deg):
    a = np.radians(deg); c, s = np.cos(a), np.sin(a)
    return {0: np.array([[1, 0, 0], [0, c, -s], [0, s, c]]), 1: np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]]),
            2: np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])}[axis]

def icp(src, dst, R0, iters=40):
    R = R0
    for _ in range(iters):
        S = src @ R.T
        d2 = ((S[:, None, :] - dst[None, :, :])**2).sum(-1)
        nn = dst[d2.argmin(1)]
        # rotation-only Kabsch about the origin (the wrist), uniform scale allowed in the fit
        H = src.T @ nn
        U, _, Vt = np.linalg.svd(H)
        D = np.diag([1, 1, np.sign(np.linalg.det(Vt.T @ U.T))])
        R = Vt.T @ D @ U.T
    err = np.sqrt(d2.min(1).mean())
    return R, err

def angle(R):
    return np.degrees(np.arccos(np.clip((np.trace(R) - 1) / 2, -1, 1)))

def fit(mario_pts, char_pts, max_deg=45):
    """Small corrective turn only: a rounded fist can fit almost as well flipped palm-over, so the
    search starts near the hand's current orientation and never turns it more than max_deg."""
    s = np.linalg.norm(mario_pts, axis=1).mean() / np.linalg.norm(char_pts, axis=1).mean()
    src = char_pts * s
    best = (np.eye(3), icp(src, mario_pts, np.eye(3), iters=1)[1])
    for roll in (-20, 0, 20):
        for pitch in (-20, 0, 20):
            R, e = icp(src, mario_pts, rot(0, roll) @ rot(2, pitch), iters=25)
            if angle(R) <= max_deg and e < best[1]: best = (R, e)
    return best

if __name__ == '__main__':
    m = open(sys.argv[1], 'rb').read(); c = open(sys.argv[2], 'rb').read()
    for shape in (5, 6):
        R, e = fit(points(m, {shape}), points(c, {shape}))
        ang = np.degrees(np.arccos(np.clip((np.trace(R) - 1) / 2, -1, 1)))
        print('shape', shape, 'residual %.2f' % e, 'rotation %.1f deg' % ang, np.round(R, 3).tolist())
