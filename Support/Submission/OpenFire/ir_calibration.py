"""Fit an OpenFIRE Square profile from measured, sight-aligned IR samples."""
from itertools import permutations
import math
import statistics
import struct

from aim_analysis import ObservationGeometry
from protocol import fit_calibration, project

TARGETS = ((.5,.5), (.5,.08), (.5,.92), (.08,.5), (.92,.5))
NAMES = ('중앙', '위쪽', '아래쪽', '왼쪽', '오른쪽')
FIELDS = ('AdjX','AdjY','TLLed','TRLed','TopOffset','BtmOffset','LftOffset','RhtOffset')


def sensor_corners(sample):
    if sample.detection.layout != 0 or not ObservationGeometry.measure(sample.detection).valid_quad:
        raise ValueError('Square LED 네 점이 모두 필요합니다.')
    # Match exact measured slots to the firmware's ordered diagnostic corners.
    # Square.cpp mirrors X about MouseMaxX=4095 before the perspective warp.
    measured = [(4095-4*x, 4*y) for x,y in sample.detection.points]
    legacy = [(x/1920*4092,y/1080*3072) for x,y in sample.camera.points[:4]]
    ordered = min(permutations(measured), key=lambda ps:sum(math.dist(a,b)**2 for a,b in zip(ps,legacy)))
    if max(math.dist(a,b) for a,b in zip(ordered,legacy)) > 8:
        raise ValueError('원시 LED와 보드 보정점이 일치하지 않습니다.')
    return tuple(ordered)


def matrix_for(sample):
    corners = sensor_corners(sample)
    return fit_calibration([(corners[i][0]/4096,corners[i][1]/3072) for i in (0,1,3,2)])


def inverse_point(matrix, target):
    a,b,c,d,e,f,g,h = matrix
    u,v = target
    aa,bb,cc,dd,ee,ff = a-u*g,b-u*h,u-c,d-v*g,e-v*h,v-f
    det = aa*ee-bb*dd
    if abs(det) < 1e-10: raise ValueError('중앙 기준을 계산할 수 없습니다.')
    return ((cc*ee-bb*ff)/det,(aa*ff-cc*dd)/det)


def center_source(samples):
    points = [inverse_point(matrix_for(s),(.5,.5)) for s in samples]
    x,y = (statistics.median(p[i] for p in points) for i in (0,1))
    if not (0 <= x < 1 and 0 <= y < 1): raise ValueError('중앙 기준이 센서 범위를 벗어납니다.')
    return x*4096,y*3072


def point_for(sample, source):
    u,v = project(matrix_for(sample),(source[0]/4096,source[1]/3072))
    # Installation guide: wide display, LEDs at 30/70 percent, top/bottom.
    return .3+(u-.1)*.5,(v-.1)/.8


def profile_point(sample, values):
    u,v = point_for(sample,(values['AdjX'],values['AdjY']))
    return (u*(1+(values['LftOffset']+values['RhtOffset'])/7680)-values['LftOffset']/7680,
            v*(1+(values['TopOffset']+values['BtmOffset'])/4320)-values['TopOffset']/4320)


def fit_profile(captures):
    if len(captures) != 5 or any(len(c) < 8 for c in captures):
        raise ValueError('중앙·위·아래·왼쪽·오른쪽의 안정된 측정이 필요합니다.')
    source = center_source(captures[0])
    observed = [tuple(statistics.median(point_for(s,source)[i] for s in batch) for i in (0,1)) for batch in captures]
    if observed[4][0]-observed[3][0] < .25 or observed[2][1]-observed[1][1] < .25:
        raise ValueError('표적 순서나 조준 방향을 확인하고 다시 보정하세요.')
    def axis(i):
        xs=[p[i] for p in observed]; ys=[p[i] for p in TARGETS]
        xm,ym=statistics.mean(xs),statistics.mean(ys)
        gain=sum((x-xm)*(y-ym) for x,y in zip(xs,ys))/sum((x-xm)**2 for x in xs)
        if not .4 <= gain <= 2.5: raise ValueError('보정 배율이 과도합니다. 설치 위치와 표적 조준을 확인하세요.')
        return gain,ym-gain*xm
    ax,bx=axis(0); ay,by=axis(1)
    values=dict(AdjX=source[0],AdjY=source[1],TLLed=2304.,TRLed=5376.,
                LftOffset=round(-bx*7680),RhtOffset=round((ax-1+bx)*7680),
                TopOffset=round(-by*4320),BtmOffset=round((ay-1+by)*4320))
    # 6.1 treats four zero offsets as an unconfigured first boot. One internal
    # unit (a quarter reference pixel) preserves an otherwise exact fit.
    if not any(values[k] for k in FIELDS[4:]): values['BtmOffset']=1
    # Match the stored float32 representation before evaluating the result.
    for key in FIELDS[:4]: values[key]=struct.unpack('<f',struct.pack('<f',values[key]))[0]
    validate_values(values)
    errors=[]
    for target,batch in zip(TARGETS,captures):
        center=tuple(statistics.median(profile_point(s,values)[i] for s in batch) for i in (0,1))
        errors.append(math.dist(center,target))
    if max(errors) > .035: raise ValueError(f'표적 간 오차가 큽니다 ({max(errors)*100:.1f}%). 다시 조준해 주세요.')
    return values,dict(observed=observed,fit_errors=errors)


def validate_values(values):
    if set(values) != set(FIELDS) or not all(math.isfinite(v) for v in values.values()):
        raise ValueError('Invalid calibration fields')
    if not (0 <= values['AdjX'] < 4096 and 0 <= values['AdjY'] < 3072):
        raise ValueError('Calibration source outside sensor')
    if (values['TLLed'],values['TRLed']) != (2304.,5376.):
        raise ValueError('Calibration requires the wide-display Square guide')
    for key,limit in (('LftOffset',7680),('RhtOffset',7680),('TopOffset',4320),('BtmOffset',4320)):
        if type(values[key]) is not int or abs(values[key]) > limit:
            raise ValueError('Invalid calibration offset')
    return values


def encoded_values(values):
    validate_values(values)
    return {key:struct.pack('<f' if key in FIELDS[:4] else '<i',values[key]).hex() for key in FIELDS}
