"""Cheap physical receiver gate, independent of all alcove transport/target labels.

20 existing Quake room AABBs + finite mirror rectangle. No old six-sphere
gargoyle occlusion, no reflected chamber visibility, no target silhouette.
Float32 definitions match the previously validated slim-shader sampler.
"""
import math
import numpy as np

ROOM_BOXES = np.array([
 [[-576,-1600,-64],[576,-512,0]], [[-576,-1600,512],[576,-512,576]],
 [[-576,-1600,0],[-512,-512,512]], [[512,-1600,0],[576,-512,512]],
 [[-576,-512,-64],[576,512,0]], [[-576,-512,512],[576,512,576]],
 [[-576,-512,0],[-512,512,512]], [[512,-512,0],[576,512,512]],
 [[-576,512,-64],[576,1600,0]], [[-576,512,512],[576,1600,576]],
 [[-576,512,0],[-512,1600,512]], [[512,512,0],[576,1600,512]],
 [[-576,-1600,0],[576,-1536,512]], [[-576,1536,0],[576,1600,512]],
 [[-576,-544,0],[-96,-512,512]], [[96,-544,0],[576,-512,512]],
 [[-96,-544,224],[96,-512,512]], [[-576,512,0],[-96,544,512]],
 [[96,512,0],[576,544,512]], [[-96,512,224],[96,544,512]]], np.float32)


def camera_rays(camera, width, height):
    forward = np.asarray(camera["forward"], np.float32)
    right = np.asarray(camera["right"], np.float32)
    up = np.asarray(camera["up"], np.float32)
    px, py = np.meshgrid((np.arange(width, dtype=np.float32) + .5) / width * 2 - 1,
                         1 - (np.arange(height, dtype=np.float32) + .5) / height * 2)
    rays = forward + right * math.tan(math.radians(camera["fov_x"]) / 2) * px[..., None] + \
        up * math.tan(math.radians(camera["fov_y"]) / 2) * py[..., None]
    rays = rays.reshape(-1, 3)
    rays /= np.linalg.norm(rays, axis=1, keepdims=True)
    return np.asarray(camera["origin"], np.float32), rays


def incident_mask(origin, directions):
    rd = np.asarray(directions, np.float32)
    ro = np.broadcast_to(np.asarray(origin, np.float32), rd.shape)
    wall = np.full(len(rd), 100000., np.float32)
    for mn, mx in ROOM_BOXES:
        near = np.full(len(rd), -100000., np.float32)
        far = np.full(len(rd), 100000., np.float32)
        valid = np.ones(len(rd), bool)
        for axis in range(3):
            parallel = np.abs(rd[:, axis]) < 1e-7
            valid &= ~(parallel & ((ro[:, axis] < mn[axis]) | (ro[:, axis] > mx[axis])))
            inv = 1 / np.where(parallel, np.float32(1), rd[:, axis])
            t1 = (mn[axis] - ro[:, axis]) * inv
            t2 = (mx[axis] - ro[:, axis]) * inv
            near = np.where(parallel, near, np.maximum(near, np.minimum(t1, t2)))
            far = np.where(parallel, far, np.minimum(far, np.maximum(t1, t2)))
            valid &= near <= far
        valid &= far > .0001
        hit = np.where(near <= .0001, far, near)
        wall = np.minimum(wall, np.where(valid & (hit > 1), hit, np.float32(100000.)))
    dx = rd[:, 0]
    safe_dx = np.where(np.abs(dx) >= 1e-6, dx, np.float32(1))
    t = (np.float32(494.) - ro[:, 0]) / safe_dx
    point = ro + rd * t[:, None]
    return ((np.abs(dx) >= 1e-6) & (t > .0001) & (np.abs(point[:, 1]) <= 384.) &
            (point[:, 2] >= 0.) & (point[:, 2] <= 432.) & (t < wall - .5))
