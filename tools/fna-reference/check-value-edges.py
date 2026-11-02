#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Audit MTH-CONF-003's exact retained dispositions; unlisted differences fail closed.

A pass is NOT FNA parity.

Run the strict comparator first to retain its unfiltered diagnostic. This separate audit uses
that same comparator at zero tolerance and accepts only the exact reviewed path/value pairs.
See docs/fna-reference-harness.md for the XNA decisions and bounded input domain.
"""
import argparse
import importlib.util
from pathlib import Path
import struct
import sys

COMPARATOR = Path(__file__).resolve().parents[2] / "scripts/compare-fna-reference.py"
SPEC = importlib.util.spec_from_file_location("compare_fna_reference", COMPARATOR)
compare = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(compare)
CATEGORIES = ("Vector2Edges", "MatrixPerspectiveEdges", "Vector3Edges", "Vector4Edges", "QuaternionEdges", "MatrixEdges", "GeometryEdges", "PackingEdges", "CurveEdges")
REVISION = "00498b0b81425b14bf8da23e68655215b9b5daa7"


def single(value):
    return struct.unpack("f", struct.pack("f", value))[0]


def matrix(entries):
    result = {f"M{row}{column}": "+zero" for row in range(1, 5) for column in range(1, 5)}
    result.update(entries)
    return result


def quaternion_retained_differences():
    # Frozen paired observations, not runtime-generated expectations. Existing decisions:
    # bd46b6ffe (wide magnitude/normalization), df9234202 (wide inverse).
    witnesses = {'nonunit': {'normalize': {'Z': (0.547722578048706, 0.5477225184440613)},
             'inverse': {'Z': (-0.10000000894069672, -0.10000000149011612)}},
 'large': {'length': ('+inf', 1.0000000200408773e+20),
           'normalize': {'X': ('+zero', 1)},
           'inverse': {'X': ('-zero', -9.999999682655225e-21)}},
 'tiny': {'length': ('+zero', 1.0000000031710769e-30),
          'normalize': {'X': ('+inf', 1),
                        'Y': ('nan', '+zero'),
                        'Z': ('nan', '+zero'),
                        'W': ('nan', '+zero')},
          'inverse': {'X': ('-inf', -1.0000000150474662e+30),
                      'Y': ('nan', '-zero'),
                      'Z': ('nan', '-zero'),
                      'W': ('nan', '+zero')}},
 'seeded0': {'normalize': {'X': (-0.47781848907470703, -0.47781845927238464),
                           'Z': (0.8409605622291565, 0.8409605026245117),
                           'W': (0.23890924453735352, 0.23890922963619232)},
             'inverse': {'X': (0.03652968257665634, 0.03652967885136604),
                         'Y': (0.006575342733412981, 0.006575342267751694),
                         'W': (0.01826484128832817, 0.01826483942568302)}},
 'seeded2': {'normalize': {'W': (0.9571124911308289, 0.9571125507354736)}},
 'seeded3': {'normalize': {'X': (-0.8655256628990173, -0.8655256032943726),
                           'W': (-0.41361403465270996, -0.4136140048503876)},
             'inverse': {'W': (-0.02534467726945877, -0.02534467540681362)}}}
    expected = {}
    for case, operations in witnesses.items():
        for operation, fields in operations.items():
            prefix = f"/QuaternionEdges/unary/{case}"
            if operation == "length":
                expected[prefix + "/length"] = fields
                continue
            overloads = (operation, operation + "Out")
            if operation == "normalize":
                overloads += ("normalizeInstance",)
            for overload in overloads:
                for component, pair in fields.items():
                    expected[f"{prefix}/{overload}/{component}"] = pair
    return expected


def matrix_retained_differences():
    # Frozen paired observations; no newly measured XNA claim. Verified existing decisions:
    # planes: e4ddea6e0 (wide Plane.Normalize and shadow normalizes its input).
    # algebra: 6c96fc674 (minor/cofactor stores narrow, determinant/reciprocal wide).
    # decomposition: 705ea2324 (ranked reflection, zero/tiny/sheared/nonfinite axes).
    witnesses = {'planes': {'nonunit': {'shadow': {'M11': (-4, -2),
                                   'M21': (-2, -1),
                                   'M23': (-6, -3),
                                   'M33': (-4, -2),
                                   'M41': (4, 2),
                                   'M42': (-8, -4),
                                   'M43': (12, 6),
                                   'M44': (-4, -2)}},
            'zero': {'shadow': {'M11': ('+zero', 'nan'),
                                'M12': ('+zero', 'nan'),
                                'M13': ('-zero', 'nan'),
                                'M21': ('-zero', 'nan'),
                                'M22': ('+zero', 'nan'),
                                'M23': ('-zero', 'nan'),
                                'M31': ('-zero', 'nan'),
                                'M32': ('+zero', 'nan'),
                                'M33': ('+zero', 'nan'),
                                'M41': (-1, '-inf'),
                                'M42': (2, '+inf'),
                                'M43': (-3, '-inf'),
                                'M44': ('+zero', 'nan')}},
            'nearUnit': {'shadow': {'M11': (-2.000000238418579, -2),
                                    'M21': (-1.0000001192092896, -1),
                                    'M23': (-3.000000476837158, -3),
                                    'M33': (-2.000000238418579, -2),
                                    'M41': (2, 1.999999761581421),
                                    'M42': (-4, -3.999999523162842),
                                    'M43': (6, 5.999999046325684),
                                    'M44': (-2.000000238418579, -2)}},
            'large': {'reflection': {'M11': (1, -1), 'M41': ('+zero', 1.999999936531045e-20)},
                      'shadow': {'M12': (2.0000000400817547e+20, 2),
                                 'M13': (-3.000000060122632e+20, -3),
                                 'M22': (1.0000000200408773e+20, 1),
                                 'M33': (1.0000000200408773e+20, 1),
                                 'M41': (1, 9.999999682655225e-21),
                                 'M42': (-2, -1.999999936531045e-20),
                                 'M43': (3, 2.9999999047965676e-20),
                                 'M44': (1.0000000200408773e+20, 1)}},
            'tiny': {'reflection': {'M11': ('-inf', -1),
                                    'M12': ('nan', '-zero'),
                                    'M13': ('nan', '-zero'),
                                    'M21': ('nan', '-zero'),
                                    'M22': ('nan', 1),
                                    'M23': ('nan', '-zero'),
                                    'M31': ('nan', '-zero'),
                                    'M32': ('nan', '-zero'),
                                    'M33': ('nan', 1),
                                    'M41': ('+inf', 2.0000000300949324e+30),
                                    'M42': ('nan', '+zero'),
                                    'M43': ('nan', '+zero')},
                     'shadow': {'M12': (2.0000000063421537e-30, 2),
                                'M13': (-3.0000000095132306e-30, -3),
                                'M22': (1.0000000031710769e-30, 1),
                                'M33': (1.0000000031710769e-30, 1),
                                'M41': (1, 1.0000000150474662e+30),
                                'M42': (-2, -2.0000000300949324e+30),
                                'M43': (3, 3.000000196258126e+30),
                                'M44': (1.0000000031710769e-30, 1)}},
            'nan': {'shadow': {'M21': ('-zero', 'nan'),
                               'M23': ('-zero', 'nan'),
                               'M31': ('-zero', 'nan'),
                               'M32': ('+zero', 'nan'),
                               'M41': (-1, 'nan'),
                               'M42': (2, 'nan'),
                               'M43': (-3, 'nan')}},
            'infinity': {'shadow': {'M12': ('+inf', 'nan'),
                                    'M13': ('-inf', 'nan'),
                                    'M22': ('+inf', 'nan'),
                                    'M33': ('+inf', 'nan'),
                                    'M41': (-1, '-zero'),
                                    'M42': (2, '+zero'),
                                    'M43': (-3, '-zero'),
                                    'M44': ('+inf', 'nan')}},
            'seeded0': {'reflection': {'M11': (0.5157385468482971, 0.5157384872436523),
                                       'M12': (-0.08716706186532974, -0.08716707676649094),
                                       'M13': (0.852300226688385, 0.8523002862930298),
                                       'M21': (-0.08716706186532974, -0.08716707676649094),
                                       'M23': (0.1534140408039093, 0.1534140557050705),
                                       'M31': (0.852300226688385, 0.8523002862930298),
                                       'M32': (0.1534140408039093, 0.1534140557050705),
                                       'M41': (0.24213072657585144, 0.24213075637817383),
                                       'M42': (0.04358353093266487, 0.04358353838324547),
                                       'M43': (-0.4261501133441925, -0.4261501431465149)},
                        'shadow': {'M11': (35.25, 2.7752628326416016),
                                   'M12': (-12.5, -0.9841356873512268),
                                   'M13': (18.75, 1.4762035608291626),
                                   'M21': (1.125, 0.08857221156358719),
                                   'M22': (26.75, 2.106050491333008),
                                   'M23': (3.375, 0.26571664214134216),
                                   'M31': (-11, -0.8660393953323364),
                                   'M32': (22, 1.7320787906646729),
                                   'M33': (-4, -0.3149232864379883),
                                   'M41': (-3.125, -0.2460339218378067),
                                   'M42': (6.25, 0.4920678436756134),
                                   'M43': (-9.375, -0.7381017804145813),
                                   'M44': (29, 2.2831950187683105)}},
            'seeded1': {'reflection': {'M41': (0.7494416832923889, 0.7494417428970337),
                                       'M42': (-0.9131128787994385, -0.9131129384040833),
                                       'M43': (0.008614271879196167, 0.008614272810518742)},
                        'shadow': {'M11': (-26.875, -1.5677986145019531),
                                   'M12': (-21.75, -1.2688230276107788),
                                   'M13': (32.625, 1.9032344818115234),
                                   'M21': (-13.25, -0.7729611396789551),
                                   'M22': (-11.25, -0.6562879085540771),
                                   'M23': (-39.75, -2.3188834190368652),
                                   'M31': (0.125, 0.007292086258530617),
                                   'M32': (-0.25, -0.014584172517061234),
                                   'M33': (-37.375, -2.1803338527679443),
                                   'M41': (-10.125, -0.5906590223312378),
                                   'M42': (20.25, 1.1813180446624756),
                                   'M43': (-30.375, -1.7719770669937134),
                                   'M44': (-37.75, -2.2022101879119873)}},
            'seeded2': {'reflection': {'M41': (-6.419939041137695, -6.4199395179748535),
                                       'M42': (0.25679755210876465, 0.25679758191108704),
                                       'M43': (-1.5407854318618774, -1.540785551071167)},
                        'shadow': {'M11': (2.5, 0.7773221135139465),
                                   'M12': (6.25, 1.9433051347732544),
                                   'M13': (-9.375, -2.9149577617645264),
                                   'M21': (0.125, 0.03886610269546509),
                                   'M22': (5.375, 1.6712424755096436),
                                   'M23': (0.375, 0.11659830808639526),
                                   'M31': (-0.75, -0.23319661617279053),
                                   'M32': (1.5, 0.46639323234558105),
                                   'M33': (3.375, 1.0493848323822021),
                                   'M41': (-10.625, -3.303618907928467),
                                   'M42': (21.25, 6.607237815856934),
                                   'M43': (-31.875, -9.910856246948242),
                                   'M44': (5.625, 1.7489746809005737)}},
            'seeded3': {'reflection': {'M23': (0.04076722264289856, 0.04076721519231796),
                                       'M32': (0.04076722264289856, 0.04076721519231796),
                                       'M43': (0.2751787304878235, 0.2751787006855011)},
                        'shadow': {'M11': (15.5, 1.043196201324463),
                                   'M12': (-28.25, -1.9013091325759888),
                                   'M13': (42.375, 2.851963758468628),
                                   'M21': (1, 0.06730297952890396),
                                   'M22': (-0.625, -0.04206438362598419),
                                   'M23': (3, 0.20190894603729248),
                                   'M31': (-4.5, -0.30286338925361633),
                                   'M32': (9, 0.6057267785072327),
                                   'M33': (-12.125, -0.8160486221313477),
                                   'M41': (6.75, 0.4542950987815857),
                                   'M42': (-13.5, -0.9085901975631714),
                                   'M43': (20.25, 1.3628852367401123),
                                   'M44': (1.375, 0.09254157543182373)}}},
 'algebra': {'seeded0': {'inverse': {'M13': (-0.07706168293952942, -0.07706167548894882)}},
             'seeded1': {'inverse': {'M12': (0.22391508519649506, 0.22391510009765625),
                                     'M14': (-0.03709173575043678, -0.03709173947572708),
                                     'M24': (0.1118178442120552, 0.1118178516626358),
                                     'M32': (-0.22777479887008667, -0.22777481377124786),
                                     'M33': (0.013235988095402718, 0.013235989026725292),
                                     'M44': (-0.012095511890947819, -0.012095512822270393)}},
             'seeded2': {'inverse': {'M12': (-0.23652265965938568, -0.2365226447582245),
                                     'M13': (-0.10200235247612, -0.1020023450255394),
                                     'M14': (-0.3609093725681305, -0.3609093427658081),
                                     'M22': (0.06566797196865082, 0.06566796451807022),
                                     'M24': (0.1230926364660263, 0.12309262901544571),
                                     'M31': (0.05068367347121239, 0.05068366974592209),
                                     'M32': (0.0490846149623394, 0.0490846112370491),
                                     'M33': (-0.05290054902434349, -0.05290054529905319),
                                     'M41': (-0.030854560434818268, -0.03085455857217312)}},
             'seeded3': {'inverse': {'M12': (-0.04159928113222122, -0.04159928485751152),
                                     'M14': (0.09351510554552078, 0.09351511299610138),
                                     'M21': (-0.10105771571397781, -0.10105772316455841),
                                     'M22': (-0.0915018618106842, -0.0915018692612648),
                                     'M31': (-0.2094096690416336, -0.2094096839427948),
                                     'M42': (-0.10121063143014908, -0.10121063888072968),
                                     'M43': (-0.04397615045309067, -0.043976154178380966),
                                     'M44': (0.05794354900717735, 0.05794355273246765)}}},
 'decomposition': {'negative': {'scale': {'Z': (4, -4)},
                                'rotation': {'Y': ('+zero', 1), 'W': (0.7071067690849304, '-zero')}},
                   'negativeTie': {'scale': {'X': (1, -1)},
                                   'rotation': {'Z': ('+zero', '-zero'), 'W': (0.7071067690849304, 1)}},
                   'zeroAxis': {'accepted': (False, True),
                                'rotation': {'X': ('+zero', 'nan'),
                                             'Y': ('+zero', 'nan'),
                                             'Z': ('+zero', 'nan'),
                                             'W': (1, 'nan')}},
                   'allZero': {'accepted': (False, True),
                               'rotation': {'X': ('+zero', 'nan'),
                                            'Y': ('+zero', 'nan'),
                                            'Z': ('+zero', 'nan'),
                                            'W': (1, 'nan')}},
                   'belowEpsilon': {'accepted': (True, False)},
                   'large': {'scale': {'X': ('+inf', 1.0000000200408773e+20)},
                             'rotation': {'W': (0.8660253882408142, 1)}},
                   'nan': {'accepted': (False, True),
                           'exception': ('ArithmeticException', 'none'),
                           'scale': {'X': (11, 'nan'), 'Y': (12, 1), 'Z': (13, 1)},
                           'rotation': {'X': (31, 'nan'),
                                        'Y': (32, 'nan'),
                                        'Z': (33, 'nan'),
                                        'W': (34, 'nan')}},
                   'infinity': {'accepted': (False, True),
                                'exception': ('ArithmeticException', 'none'),
                                'scale': {'X': (11, '+inf'), 'Y': (12, 1), 'Z': (13, 1)},
                                'rotation': {'X': (31, 'nan'),
                                             'Y': (32, 'nan'),
                                             'Z': (33, 'nan'),
                                             'W': (34, 'nan')}},
                   'shear': {'accepted': (True, False),
                             'rotation': {'Z': (0.11330867558717728, '+zero'), 'W': (0.9867151975631714, 1)}},
                   'seeded1': {'scale': {'Z': (13.25, -13.25)},
                               'rotation': {'X': ('+zero', 1), 'W': (0.7071067690849304, '+zero')}},
                   'seeded2': {'scale': {'Y': (10.125, -10.125)},
                               'rotation': {'Z': ('+zero', 1), 'W': (0.7071067690849304, '+zero')}},
                   'seeded3': {'scale': {'Z': (10.625, -10.625)},
                               'rotation': {'Y': ('+zero', 1), 'W': (0.7071067690849304, '-zero')}}}}
    expected = {}
    for group, cases in witnesses.items():
        for case, operations in cases.items():
            for operation, fields in operations.items():
                overloads = [operation]
                if group == "planes":
                    overloads += [operation + "Out"]
                elif group == "algebra":
                    overloads += [operation + "Out", operation + "Aliased"]
                for overload in overloads:
                    prefix = f"/MatrixEdges/{group}/{case}/{overload}"
                    if isinstance(fields, dict):
                        expected.update({prefix + "/" + field: pair for field, pair in fields.items()})
                    else:
                        expected[prefix] = fields
    return expected


def geometry_retained_differences():
    # Frozen paired observations under existing decisions, not a new XNA run:
    # e4ddea6e0: Plane.Normalize wide magnitude, reciprocal, Single sqrt store.
    # e1377c437: Ray/box nonnegative interval and strict parallel threshold.
    # 5ab1c7729: strict sphere surface/tangency, signed radius containment, GJK volumes.
    # 52f797f60, f46f26ea8, f5063e08c: sphere seed ties, wide growth and span selection.
    expected = {'/GeometryEdges/planes/large/normalize/normal/X': ('+zero', 1),
 '/GeometryEdges/planes/large/normalize/d': ('-zero', -9.999999682655225e-21),
 '/GeometryEdges/planes/tiny/normalize/normal/X': ('+inf', 1),
 '/GeometryEdges/planes/tiny/normalize/normal/Y': ('nan', '+zero'),
 '/GeometryEdges/planes/tiny/normalize/normal/Z': ('nan', '+zero'),
 '/GeometryEdges/planes/tiny/normalize/d': ('-inf', -1.0000000150474662e+30),
 '/GeometryEdges/planes/seeded0/normalize/normal/X': (-0.492067813873291, -0.4920678436756134),
 '/GeometryEdges/planes/seeded0/normalize/normal/Y': (-0.08857220411300659, -0.08857221156358719),
 '/GeometryEdges/planes/seeded0/normalize/d': (0.2460339069366455, 0.2460339218378067),
 '/GeometryEdges/planes/seeded1/normalize/d': (0.590658962726593, 0.5906590223312378),
 '/GeometryEdges/planes/seeded2/normalize/d': (3.3036186695098877, 3.303618907928467),
 '/GeometryEdges/planes/seeded3/normalize/normal/Z': (0.3028634190559387, 0.30286338925361633),
 '/GeometryEdges/relations/boundary/sphereIntersectsSphere': (True, False),
 '/GeometryEdges/relations/boundary/sphereIntersectsSphereOut': (True, False),
 '/GeometryEdges/relations/disjoint/boxContainsFrustum': (2, 0),
 '/GeometryEdges/relations/disjoint/sphereContainsFrustum': (2, 0),
 '/GeometryEdges/relations/zero/sphereContainsSphere': (1, 2),
 '/GeometryEdges/relations/zero/sphereContainsSphereOut': (1, 2),
 '/GeometryEdges/relations/zero/sphereContainsPoint': (2, 0),
 '/GeometryEdges/relations/zero/sphereContainsPointOut': (2, 0),
 '/GeometryEdges/relations/zero/boxIntersectsRay': ('none', '+zero'),
 '/GeometryEdges/relations/zero/boxIntersectsRayOut': ('none', '+zero'),
 '/GeometryEdges/relations/zero/rayIntersectsBox': ('none', '+zero'),
 '/GeometryEdges/relations/zero/rayIntersectsBoxOut': ('none', '+zero'),
 '/GeometryEdges/relations/insideOut/boxContainsFrustum': (2, 0),
 '/GeometryEdges/relations/negativeRadius/sphereContainsPoint': (2, 0),
 '/GeometryEdges/relations/negativeRadius/sphereContainsPointOut': (2, 0),
 '/GeometryEdges/relations/stationary/boxIntersectsRay': ('none', '+zero'),
 '/GeometryEdges/relations/stationary/boxIntersectsRayOut': ('none', '+zero'),
 '/GeometryEdges/relations/stationary/rayIntersectsBox': ('none', '+zero'),
 '/GeometryEdges/relations/stationary/rayIntersectsBoxOut': ('none', '+zero'),
 '/GeometryEdges/relations/threshold0/boxIntersectsRay': (2000000, 'none'),
 '/GeometryEdges/relations/threshold0/boxIntersectsRayOut': (2000000, 'none'),
 '/GeometryEdges/relations/threshold0/rayIntersectsBox': (2000000, 'none'),
 '/GeometryEdges/relations/threshold0/rayIntersectsBoxOut': (2000000, 'none'),
 '/GeometryEdges/relations/seeded1/boxContainsFrustum': (2, 0),
 '/GeometryEdges/relations/seeded3/boxContainsFrustum': (2, 0),
 '/GeometryEdges/relations/seeded4/sphereContainsFrustum': (2, 0),
 '/GeometryEdges/relations/seeded5/boxContainsFrustum': (2, 0),
 '/GeometryEdges/relations/seeded5/sphereContainsFrustum': (2, 0),
 '/GeometryEdges/pointSets/triangleTie/sphere/center/X': (0.36180341243743896, 0.27639320492744446),
 '/GeometryEdges/pointSets/triangleTie/sphere/center/Y': (0.27639320492744446, 0.36180341243743896),
 '/GeometryEdges/pointSets/largePair/sphere/radius': ('+inf', 1.0000000200408773e+20),
 '/GeometryEdges/pointSets/tinyPair/sphere/radius': ('+zero', 1.0000000031710769e-30),
 '/GeometryEdges/pointSets/seeded0/sphere/center/X': (-4.443966865539551, -0.5894544124603271),
 '/GeometryEdges/pointSets/seeded0/sphere/center/Y': (-2.781680107116699, -0.4585781693458557),
 '/GeometryEdges/pointSets/seeded0/sphere/center/Z': (7.745290279388428, 7.6943888664245605),
 '/GeometryEdges/pointSets/seeded0/sphere/radius': (14.372993469238281, 13.917911529541016),
 '/GeometryEdges/pointSets/seeded3/sphere/center/X': (-3.6574785709381104, -3.6574783325195312),
 '/GeometryEdges/pointSets/seeded3/sphere/center/Y': (1.7255644798278809, 1.7255642414093018),
 '/GeometryEdges/pointSets/seeded3/sphere/center/Z': (-3.630509376525879, -3.6305088996887207),
 '/GeometryEdges/frustums/identity/sphere/center/X': (-0.012568414211273193, -0.012568328529596329),
 '/GeometryEdges/frustums/identity/sphere/center/Y': (0.07666969299316406, 0.07666976004838943),
 '/GeometryEdges/frustums/identity/sphere/center/Z': (0.15762576460838318, 0.15762582421302795),
 '/GeometryEdges/frustums/identity/sphere/radius': (1.7012075185775757, 1.7012073993682861),
 '/GeometryEdges/frustums/touching/contains': (2, 0),
 '/GeometryEdges/frustums/touching/reverseContains': (2, 0),
 '/GeometryEdges/frustums/touching/intersects': (True, False),
 '/GeometryEdges/frustums/touching/reverseIntersects': (True, False),
 '/GeometryEdges/frustums/touching/sphere/center/X': (1.987431526184082, 1.9874316453933716),
 '/GeometryEdges/frustums/touching/sphere/center/Y': (0.07666969299316406, 0.07666976004838943),
 '/GeometryEdges/frustums/touching/sphere/center/Z': (0.15762576460838318, 0.15762582421302795),
 '/GeometryEdges/frustums/touching/sphere/radius': (1.7012075185775757, 1.7012073993682861),
 '/GeometryEdges/frustums/separated/sphere/center/X': (2.987431764602661, 2.987431526184082),
 '/GeometryEdges/frustums/separated/sphere/center/Y': (0.07666969299316406, 0.07666968554258347),
 '/GeometryEdges/frustums/separated/sphere/center/Z': (0.15762576460838318, 0.15762588381767273),
 '/GeometryEdges/frustums/separated/sphere/radius': (1.7012072801589966, 1.7012073993682861),
 '/GeometryEdges/frustums/enclosing/sphere/center/X': (-0.025136828422546387, -0.025136657059192657),
 '/GeometryEdges/frustums/enclosing/sphere/center/Y': (0.15333938598632812, 0.15333952009677887),
 '/GeometryEdges/frustums/enclosing/sphere/center/Z': (0.31525152921676636, 0.3152516484260559),
 '/GeometryEdges/frustums/enclosing/sphere/radius': (3.4024150371551514, 3.4024147987365723),
 '/GeometryEdges/frustums/enclosed/sphere/center/X': (-0.006284207105636597, -0.006284164264798164),
 '/GeometryEdges/frustums/enclosed/sphere/center/Y': (0.03833484649658203, 0.03833488002419472),
 '/GeometryEdges/frustums/enclosed/sphere/center/Z': (0.07881288230419159, 0.07881291210651398),
 '/GeometryEdges/frustums/enclosed/sphere/radius': (0.8506037592887878, 0.8506036996841431)}
    # Integration-owner decision, 2026-11-02, under 5ab1c7729:
    # algorithmic consequence of the measured XNA GJK port; negative radii were not separately measured against XNA
    expected.update({
        '/GeometryEdges/relations/negativeRadius/sphereIntersectsFrustum': (False, True),
        '/GeometryEdges/relations/negativeRadius/frustumIntersectsSphere': (False, True),
        '/GeometryEdges/relations/negativeRadius/frustumIntersectsSphereOut': (False, True),
    })
    for path, pair in list(expected.items()):
        if "/planes/" in path and "/normalize/" in path:
            for form in ("normalizeOut", "normalizeInstance", "normalizeAliased"):
                expected[path.replace("/normalize/", "/" + form + "/")] = pair
    return expected


def packing_retained_differences():
    # Verified existing decisions: b6f127b15 (SNORM minimum code expands to -1),
    # 1ad0a3916 (Color fixed-point Lerp/Multiply, including NaN/infinite factors).
    return {'/PackingEdges/packedTypes/NormalizedByte2/negative/rawUnpack/Y': (-1.0078740119934082, -1),
 '/PackingEdges/packedTypes/NormalizedByte2/ties/rawUnpack/Y': (-1.0078740119934082, -1),
 '/PackingEdges/packedTypes/NormalizedByte2/clampHigh/rawUnpack/Y': (-1.0078740119934082, -1),
 '/PackingEdges/packedTypes/NormalizedByte2/shortBoundary/rawUnpack/Y': (-1.0078740119934082, -1),
 '/PackingEdges/packedTypes/NormalizedByte2/seeded0/rawUnpack/Y': (-1.0078740119934082, -1),
 '/PackingEdges/packedTypes/NormalizedByte2/seeded2/rawUnpack/Y': (-1.0078740119934082, -1),
 '/PackingEdges/packedTypes/NormalizedByte4/negative/rawUnpack/Y': (-1.0078740119934082, -1),
 '/PackingEdges/packedTypes/NormalizedByte4/negative/rawUnpack/W': (-1.0078740119934082, -1),
 '/PackingEdges/packedTypes/NormalizedByte4/ties/rawUnpack/Y': (-1.0078740119934082, -1),
 '/PackingEdges/packedTypes/NormalizedByte4/clampHigh/rawUnpack/Y': (-1.0078740119934082, -1),
 '/PackingEdges/packedTypes/NormalizedByte4/clampHigh/rawUnpack/W': (-1.0078740119934082, -1),
 '/PackingEdges/packedTypes/NormalizedByte4/shortBoundary/rawUnpack/Y': (-1.0078740119934082, -1),
 '/PackingEdges/packedTypes/NormalizedByte4/seeded0/rawUnpack/Y': (-1.0078740119934082, -1),
 '/PackingEdges/packedTypes/NormalizedByte4/seeded0/rawUnpack/W': (-1.0078740119934082, -1),
 '/PackingEdges/packedTypes/NormalizedByte4/seeded2/rawUnpack/Y': (-1.0078740119934082, -1),
 '/PackingEdges/packedTypes/NormalizedShort2/negative/rawUnpack/X': (-1.000030517578125, -1),
 '/PackingEdges/packedTypes/NormalizedShort2/negative/rawUnpack/Y': (-1.000030517578125, -1),
 '/PackingEdges/packedTypes/NormalizedShort2/ties/rawUnpack/X': (-1.000030517578125, -1),
 '/PackingEdges/packedTypes/NormalizedShort2/clampHigh/rawUnpack/X': (-1.000030517578125, -1),
 '/PackingEdges/packedTypes/NormalizedShort2/clampHigh/rawUnpack/Y': (-1.000030517578125, -1),
 '/PackingEdges/packedTypes/NormalizedShort2/shortBoundary/rawUnpack/X': (-1.000030517578125, -1),
 '/PackingEdges/packedTypes/NormalizedShort2/seeded0/rawUnpack/X': (-1.000030517578125, -1),
 '/PackingEdges/packedTypes/NormalizedShort2/seeded0/rawUnpack/Y': (-1.000030517578125, -1),
 '/PackingEdges/packedTypes/NormalizedShort2/seeded2/rawUnpack/X': (-1.000030517578125, -1),
 '/PackingEdges/packedTypes/NormalizedShort4/negative/rawUnpack/X': (-1.000030517578125, -1),
 '/PackingEdges/packedTypes/NormalizedShort4/negative/rawUnpack/Y': (-1.000030517578125, -1),
 '/PackingEdges/packedTypes/NormalizedShort4/negative/rawUnpack/Z': (-1.000030517578125, -1),
 '/PackingEdges/packedTypes/NormalizedShort4/negative/rawUnpack/W': (-1.000030517578125, -1),
 '/PackingEdges/packedTypes/NormalizedShort4/ties/rawUnpack/X': (-1.000030517578125, -1),
 '/PackingEdges/packedTypes/NormalizedShort4/clampHigh/rawUnpack/X': (-1.000030517578125, -1),
 '/PackingEdges/packedTypes/NormalizedShort4/clampHigh/rawUnpack/Y': (-1.000030517578125, -1),
 '/PackingEdges/packedTypes/NormalizedShort4/clampHigh/rawUnpack/Z': (-1.000030517578125, -1),
 '/PackingEdges/packedTypes/NormalizedShort4/clampHigh/rawUnpack/W': (-1.000030517578125, -1),
 '/PackingEdges/packedTypes/NormalizedShort4/shortBoundary/rawUnpack/X': (-1.000030517578125, -1),
 '/PackingEdges/packedTypes/NormalizedShort4/seeded0/rawUnpack/X': (-1.000030517578125, -1),
 '/PackingEdges/packedTypes/NormalizedShort4/seeded0/rawUnpack/Y': (-1.000030517578125, -1),
 '/PackingEdges/packedTypes/NormalizedShort4/seeded0/rawUnpack/Z': (-1.000030517578125, -1),
 '/PackingEdges/packedTypes/NormalizedShort4/seeded0/rawUnpack/W': (-1.000030517578125, -1),
 '/PackingEdges/packedTypes/NormalizedShort4/seeded2/rawUnpack/X': (-1.000030517578125, -1),
 '/PackingEdges/interpolation/case8/lerp/packed': ('0000000000000000', '00000000fe0001ff'),
 '/PackingEdges/interpolation/case8/lerp/r': (0, 255),
 '/PackingEdges/interpolation/case8/lerp/g': (0, 1),
 '/PackingEdges/interpolation/case8/lerp/a': (0, 254),
 '/PackingEdges/interpolation/case8/lerp/vector3/X': ('+zero', 1),
 '/PackingEdges/interpolation/case8/lerp/vector3/Y': ('+zero', 0.003921568859368563),
 '/PackingEdges/interpolation/case8/lerp/vector4/X': ('+zero', 1),
 '/PackingEdges/interpolation/case8/lerp/vector4/Y': ('+zero', 0.003921568859368563),
 '/PackingEdges/interpolation/case8/lerp/vector4/W': ('+zero', 0.9960784316062927),
 '/PackingEdges/interpolation/case9/multiply/packed': ('0000000000000000', '00000000ff00ffff'),
 '/PackingEdges/interpolation/case9/multiply/r': (0, 255),
 '/PackingEdges/interpolation/case9/multiply/g': (0, 255),
 '/PackingEdges/interpolation/case9/multiply/a': (0, 255),
 '/PackingEdges/interpolation/case9/multiply/vector3/X': ('+zero', 1),
 '/PackingEdges/interpolation/case9/multiply/vector3/Y': ('+zero', 1),
 '/PackingEdges/interpolation/case9/multiply/vector4/X': ('+zero', 1),
 '/PackingEdges/interpolation/case9/multiply/vector4/Y': ('+zero', 1),
 '/PackingEdges/interpolation/case9/multiply/vector4/W': ('+zero', 1),
 '/PackingEdges/interpolation/case9/operator/packed': ('0000000000000000', '00000000ff00ffff'),
 '/PackingEdges/interpolation/case9/operator/r': (0, 255),
 '/PackingEdges/interpolation/case9/operator/g': (0, 255),
 '/PackingEdges/interpolation/case9/operator/a': (0, 255),
 '/PackingEdges/interpolation/case9/operator/vector3/X': ('+zero', 1),
 '/PackingEdges/interpolation/case9/operator/vector3/Y': ('+zero', 1),
 '/PackingEdges/interpolation/case9/operator/vector4/X': ('+zero', 1),
 '/PackingEdges/interpolation/case9/operator/vector4/Y': ('+zero', 1),
 '/PackingEdges/interpolation/case9/operator/vector4/W': ('+zero', 1),
 '/PackingEdges/interpolation/case11/lerp/packed': ('00000000fd0001fe', '00000000fe0001ff'),
 '/PackingEdges/interpolation/case11/lerp/r': (254, 255),
 '/PackingEdges/interpolation/case11/lerp/a': (253, 254),
 '/PackingEdges/interpolation/case11/lerp/vector3/X': (0.9960784316062927, 1),
 '/PackingEdges/interpolation/case11/lerp/vector4/X': (0.9960784316062927, 1),
 '/PackingEdges/interpolation/case11/lerp/vector4/W': (0.9921568632125854, 0.9960784316062927),
 '/PackingEdges/interpolation/case13/lerp/packed': ('000000000000fd00', '000000000001fe00'),
 '/PackingEdges/interpolation/case13/lerp/g': (253, 254),
 '/PackingEdges/interpolation/case13/lerp/b': (0, 1),
 '/PackingEdges/interpolation/case13/lerp/vector3/Y': (0.9921568632125854, 0.9960784316062927),
 '/PackingEdges/interpolation/case13/lerp/vector3/Z': ('+zero', 0.003921568859368563),
 '/PackingEdges/interpolation/case13/lerp/vector4/Y': (0.9921568632125854, 0.9960784316062927),
 '/PackingEdges/interpolation/case13/lerp/vector4/Z': ('+zero', 0.003921568859368563)}


def half_retained_differences():
    # Microsoft.Xna.Framework.dll 4.0.0.0, token 842cf8be1de50553; HalfUtils.Pack
    # IL_001b..IL_008e saturates/rounds; Unpack IL_005f..IL_008a keeps exponent 31 finite.
    # Framework DLL SHA256 38e7093f52d7474bbc6256906519781a1210d7da50a1c667b52716fcf49ca130.
    # See xna-half/README.md for local regeneration; no disassembly is distributed.
    # Mono could not initialize the mixed-mode
    # original assembly; this decision rests on the shipped IL, not a successful runtime probe.
    return {'/PackingEdges/packedTypes/HalfSingle/one/repackedRaw': ('000000000000fc00', '000000000000ffff'),
 '/PackingEdges/packedTypes/HalfSingle/quarters/repackedRaw': ('0000000000007c00', '0000000000007fff'),
 '/PackingEdges/packedTypes/HalfSingle/clampLow/repackedRaw': ('000000000000fc00', '000000000000ffff'),
 '/PackingEdges/packedTypes/HalfSingle/byteBoundary/repackedRaw': ('0000000000007c00', '0000000000007fff'),
 '/PackingEdges/packedTypes/HalfSingle/nonfinite/constructor': ('0000000000007e00', '0000000000007fff'),
 '/PackingEdges/packedTypes/HalfSingle/nonfinite/vectorConstructor': ('0000000000007e00', '0000000000007fff'),
 '/PackingEdges/packedTypes/HalfSingle/nonfinite/unpack/X': (98304, 131008),
 '/PackingEdges/packedTypes/HalfSingle/nonfinite/vectorUnpack/X': (98304, 131008),
 '/PackingEdges/packedTypes/HalfSingle/nonfinite/pack': ('0000000000007e00', '0000000000007fff'),
 '/PackingEdges/packedTypes/HalfSingle/nonfinite/packUnpack/X': (98304, 131008),
 '/PackingEdges/packedTypes/HalfSingle/nonfinite/repackedRaw': ('000000000000fc00', '000000000000ffff'),
 '/PackingEdges/packedTypes/HalfSingle/seeded1/repackedRaw': ('0000000000007c00', '0000000000007fff'),
 '/PackingEdges/packedTypes/HalfVector2/one/repackedRaw': ('00000000fc00fc00', '00000000ffffffff'),
 '/PackingEdges/packedTypes/HalfVector2/quarters/repackedRaw': ('000000007c007c00', '000000007fff7fff'),
 '/PackingEdges/packedTypes/HalfVector2/clampLow/repackedRaw': ('00000000fc00fc00', '00000000ffffffff'),
 '/PackingEdges/packedTypes/HalfVector2/byteBoundary/repackedRaw': ('000000007c007c00', '000000007fff7fff'),
 '/PackingEdges/packedTypes/HalfVector2/nonfinite/constructor': ('000000007c007e00', '000000007fff7fff'),
 '/PackingEdges/packedTypes/HalfVector2/nonfinite/vectorConstructor': ('000000007c007e00', '000000007fff7fff'),
 '/PackingEdges/packedTypes/HalfVector2/nonfinite/unpack/X': (98304, 131008),
 '/PackingEdges/packedTypes/HalfVector2/nonfinite/unpack/Y': (65536, 131008),
 '/PackingEdges/packedTypes/HalfVector2/nonfinite/vectorUnpack/X': (98304, 131008),
 '/PackingEdges/packedTypes/HalfVector2/nonfinite/vectorUnpack/Y': (65536, 131008),
 '/PackingEdges/packedTypes/HalfVector2/nonfinite/pack': ('000000007c007e00', '000000007fff7fff'),
 '/PackingEdges/packedTypes/HalfVector2/nonfinite/packUnpack/X': (98304, 131008),
 '/PackingEdges/packedTypes/HalfVector2/nonfinite/packUnpack/Y': (65536, 131008),
 '/PackingEdges/packedTypes/HalfVector2/nonfinite/repackedRaw': ('00000000fc00fc00', '00000000ffffffff'),
 '/PackingEdges/packedTypes/HalfVector2/seeded1/repackedRaw': ('000000007c007c00', '000000007fff7fff'),
 '/PackingEdges/packedTypes/HalfVector4/one/repackedRaw': ('fc00fc00fc00fc00', 'ffffffffffffffff'),
 '/PackingEdges/packedTypes/HalfVector4/quarters/repackedRaw': ('7c007c007c007c00', '7fff7fff7fff7fff'),
 '/PackingEdges/packedTypes/HalfVector4/ties/repackedRaw': ('7c007c00fc008000', '7e007c00fc008000'),
 '/PackingEdges/packedTypes/HalfVector4/clampLow/repackedRaw': ('fc00fc00fc00fc00', 'ffffffffffffffff'),
 '/PackingEdges/packedTypes/HalfVector4/byteBoundary/repackedRaw': ('7c007c007c007c00', '7fff7fff7fff7fff'),
 '/PackingEdges/packedTypes/HalfVector4/shortBoundary/repackedRaw': ('7c007c00fc008000', '7e007c00fc008000'),
 '/PackingEdges/packedTypes/HalfVector4/nonfinite/constructor': ('8000fc007c007e00', '8000ffff7fff7fff'),
 '/PackingEdges/packedTypes/HalfVector4/nonfinite/vectorConstructor': ('8000fc007c007e00', '8000ffff7fff7fff'),
 '/PackingEdges/packedTypes/HalfVector4/nonfinite/unpack/X': (98304, 131008),
 '/PackingEdges/packedTypes/HalfVector4/nonfinite/unpack/Y': (65536, 131008),
 '/PackingEdges/packedTypes/HalfVector4/nonfinite/unpack/Z': (-65536, -131008),
 '/PackingEdges/packedTypes/HalfVector4/nonfinite/vectorUnpack/X': (98304, 131008),
 '/PackingEdges/packedTypes/HalfVector4/nonfinite/vectorUnpack/Y': (65536, 131008),
 '/PackingEdges/packedTypes/HalfVector4/nonfinite/vectorUnpack/Z': (-65536, -131008),
 '/PackingEdges/packedTypes/HalfVector4/nonfinite/pack': ('8000fc007c007e00', '8000ffff7fff7fff'),
 '/PackingEdges/packedTypes/HalfVector4/nonfinite/packUnpack/X': (98304, 131008),
 '/PackingEdges/packedTypes/HalfVector4/nonfinite/packUnpack/Y': (65536, 131008),
 '/PackingEdges/packedTypes/HalfVector4/nonfinite/packUnpack/Z': (-65536, -131008),
 '/PackingEdges/packedTypes/HalfVector4/nonfinite/repackedRaw': ('fc00fc00fc00fc00', 'ffffffffffffffff'),
 '/PackingEdges/packedTypes/HalfVector4/seeded1/repackedRaw': ('7c007c007c007c00', '7fff7fff7fff7fff'),
 '/PackingEdges/packedTypes/HalfVector4/seeded2/repackedRaw': ('7c007c00fc008000', '7e007c00fc008000')}


def curve_retained_differences():
    # Verified d753d0997: NaN scan outcome, normalized Step threshold, degenerate/tiny
    # segment selection and post-linear last tangent. Explicit observations/loop names;
    # Insertion/tangent witnesses are separately settled by the shipped Curve IL below.
    witnesses = [('smooth',
  'p11',
  ('pre0post0',
   'pre0post1',
   'pre0post2',
   'pre0post3',
   'pre0post4',
   'pre1post0',
   'pre1post1',
   'pre1post2',
   'pre1post3',
   'pre1post4',
   'pre2post0',
   'pre2post1',
   'pre2post2',
   'pre2post3',
   'pre2post4',
   'pre3post0',
   'pre3post1',
   'pre3post2',
   'pre3post3',
   'pre3post4',
   'pre4post0',
   'pre4post1',
   'pre4post2',
   'pre4post3',
   'pre4post4'),
  ('+zero', 'nan')),
 ('smooth', 'p8', ('pre0post4', 'pre1post4', 'pre2post4', 'pre3post4', 'pre4post4'), (9.5, 15.5)),
 ('smooth', 'p9', ('pre0post4', 'pre1post4', 'pre2post4', 'pre3post4', 'pre4post4'), (14, 26)),
 ('smooth', 'p10', ('pre0post4', 'pre1post4', 'pre2post4', 'pre3post4', 'pre4post4'), (23, 47)),
 ('stepShifted',
  'p3',
  ('pre0post0',
   'pre0post1',
   'pre0post2',
   'pre0post3',
   'pre0post4',
   'pre1post0',
   'pre1post1',
   'pre1post2',
   'pre1post3',
   'pre1post4',
   'pre2post0',
   'pre2post1',
   'pre2post2',
   'pre2post3',
   'pre2post4',
   'pre3post0',
   'pre3post1',
   'pre3post2',
   'pre3post3',
   'pre3post4',
   'pre4post0',
   'pre4post1',
   'pre4post2',
   'pre4post3',
   'pre4post4'),
  (9, 1)),
 ('stepShifted',
  'p4',
  ('pre0post0',
   'pre0post1',
   'pre0post2',
   'pre0post3',
   'pre0post4',
   'pre1post0',
   'pre1post1',
   'pre1post2',
   'pre1post3',
   'pre1post4',
   'pre2post0',
   'pre2post1',
   'pre2post2',
   'pre2post3',
   'pre2post4',
   'pre3post0',
   'pre3post1',
   'pre3post2',
   'pre3post3',
   'pre3post4',
   'pre4post0',
   'pre4post1',
   'pre4post2',
   'pre4post3',
   'pre4post4'),
  (9, 1)),
 ('stepShifted',
  'p5',
  ('pre0post0',
   'pre0post1',
   'pre0post2',
   'pre0post3',
   'pre0post4',
   'pre1post0',
   'pre1post1',
   'pre1post2',
   'pre1post3',
   'pre1post4',
   'pre2post0',
   'pre2post1',
   'pre2post2',
   'pre2post3',
   'pre2post4',
   'pre3post0',
   'pre3post1',
   'pre3post2',
   'pre3post3',
   'pre3post4',
   'pre4post0',
   'pre4post1',
   'pre4post2',
   'pre4post3',
   'pre4post4'),
  (9, 1)),
 ('stepShifted',
  'p6',
  ('pre0post0',
   'pre0post1',
   'pre0post2',
   'pre0post3',
   'pre0post4',
   'pre1post0',
   'pre1post1',
   'pre1post2',
   'pre1post3',
   'pre1post4',
   'pre2post0',
   'pre2post1',
   'pre2post2',
   'pre2post3',
   'pre2post4',
   'pre3post0',
   'pre3post1',
   'pre3post2',
   'pre3post3',
   'pre3post4',
   'pre4post0',
   'pre4post1',
   'pre4post2',
   'pre4post3',
   'pre4post4'),
  (9, 1)),
 ('stepShifted',
  'p11',
  ('pre0post0',
   'pre0post1',
   'pre0post2',
   'pre0post3',
   'pre0post4',
   'pre1post0',
   'pre1post1',
   'pre1post2',
   'pre1post3',
   'pre1post4',
   'pre2post0',
   'pre2post1',
   'pre2post2',
   'pre2post3',
   'pre2post4',
   'pre3post0',
   'pre3post1',
   'pre3post2',
   'pre3post3',
   'pre3post4',
   'pre4post0',
   'pre4post1',
   'pre4post2',
   'pre4post3',
   'pre4post4'),
  ('+zero', 9)),
 ('stepShifted',
  'p8',
  ('pre0post1',
   'pre0post3',
   'pre1post1',
   'pre1post3',
   'pre2post1',
   'pre2post3',
   'pre3post1',
   'pre3post3',
   'pre4post1',
   'pre4post3'),
  (9, 1)),
 ('stepShifted',
  'p9',
  ('pre0post1',
   'pre0post3',
   'pre1post1',
   'pre1post3',
   'pre2post1',
   'pre2post3',
   'pre3post1',
   'pre3post3',
   'pre4post1',
   'pre4post3'),
  (9, 1)),
 ('stepShifted', 'p10', ('pre0post1', 'pre1post1', 'pre2post1', 'pre3post1', 'pre4post1'), (9, 1)),
 ('stepShifted', 'p8', ('pre0post2', 'pre1post2', 'pre2post2', 'pre3post2', 'pre4post2'), (17, 9)),
 ('stepShifted', 'p9', ('pre0post2', 'pre1post2', 'pre2post2', 'pre3post2', 'pre4post2'), (25, 17)),
 ('stepShifted', 'p10', ('pre0post2', 'pre1post2', 'pre2post2', 'pre3post2', 'pre4post2'), (33, 25)),
 ('stepShifted',
  'p2',
  ('pre1post0',
   'pre1post1',
   'pre1post2',
   'pre1post3',
   'pre1post4',
   'pre3post0',
   'pre3post1',
   'pre3post2',
   'pre3post3',
   'pre3post4'),
  (9, 1)),
 ('stepShifted', 'p2', ('pre2post0', 'pre2post1', 'pre2post2', 'pre2post3', 'pre2post4'), (1, -7)),
 ('stepShifted', 'p0', ('pre3post0', 'pre3post1', 'pre3post2', 'pre3post3', 'pre3post4'), (9, 1)),
 ('duplicateStart',
  'p3',
  ('pre0post0',
   'pre0post1',
   'pre0post2',
   'pre0post3',
   'pre0post4',
   'pre1post0',
   'pre1post1',
   'pre1post2',
   'pre1post3',
   'pre1post4',
   'pre2post0',
   'pre2post1',
   'pre2post2',
   'pre2post3',
   'pre2post4',
   'pre3post0',
   'pre3post1',
   'pre3post2',
   'pre3post3',
   'pre3post4',
   'pre4post0',
   'pre4post1',
   'pre4post2',
   'pre4post3',
   'pre4post4'),
  ('nan', '+zero')),
 ('duplicateStart',
  'p4',
  ('pre0post0',
   'pre0post1',
   'pre0post2',
   'pre0post3',
   'pre0post4',
   'pre1post0',
   'pre1post1',
   'pre1post2',
   'pre1post3',
   'pre1post4',
   'pre2post0',
   'pre2post1',
   'pre2post2',
   'pre2post3',
   'pre2post4',
   'pre3post0',
   'pre3post1',
   'pre3post2',
   'pre3post3',
   'pre3post4',
   'pre4post0',
   'pre4post1',
   'pre4post2',
   'pre4post3',
   'pre4post4'),
  ('nan', '+zero')),
 ('duplicateStart',
  'p11',
  ('pre0post0',
   'pre0post1',
   'pre0post2',
   'pre0post3',
   'pre0post4',
   'pre1post0',
   'pre1post1',
   'pre1post2',
   'pre1post3',
   'pre1post4',
   'pre2post0',
   'pre2post1',
   'pre2post2',
   'pre2post3',
   'pre2post4',
   'pre3post0',
   'pre3post1',
   'pre3post2',
   'pre3post3',
   'pre3post4',
   'pre4post0',
   'pre4post1',
   'pre4post2',
   'pre4post3',
   'pre4post4'),
  ('+zero', 'nan')),
 ('duplicateStart',
  'p9',
  ('pre0post1',
   'pre0post3',
   'pre1post1',
   'pre1post3',
   'pre2post1',
   'pre2post3',
   'pre3post1',
   'pre3post3',
   'pre4post1',
   'pre4post3'),
  ('nan', '+zero')),
 ('duplicateStart', 'p10', ('pre0post1', 'pre1post1', 'pre2post1', 'pre3post1', 'pre4post1'), ('nan', '+zero')),
 ('duplicateStart', 'p9', ('pre0post2', 'pre1post2', 'pre2post2', 'pre3post2', 'pre4post2'), ('nan', 40)),
 ('duplicateStart', 'p10', ('pre0post2', 'pre1post2', 'pre2post2', 'pre3post2', 'pre4post2'), ('nan', 60)),
 ('duplicateStart', 'p0', ('pre3post0', 'pre3post1', 'pre3post2', 'pre3post3', 'pre3post4'), ('nan', '+zero')),
 ('allEqual',
  'p0',
  ('pre0post0',
   'pre0post1',
   'pre0post2',
   'pre0post3',
   'pre0post4',
   'pre1post0',
   'pre1post1',
   'pre1post2',
   'pre1post3',
   'pre1post4',
   'pre2post0',
   'pre2post1',
   'pre2post2',
   'pre2post3',
   'pre2post4',
   'pre3post0',
   'pre3post1',
   'pre3post2',
   'pre3post3',
   'pre3post4',
   'pre4post0',
   'pre4post1',
   'pre4post2',
   'pre4post3',
   'pre4post4'),
  ('nan', 3)),
 ('allEqual',
  'p1',
  ('pre0post0',
   'pre0post1',
   'pre0post2',
   'pre0post3',
   'pre0post4',
   'pre1post0',
   'pre1post1',
   'pre1post2',
   'pre1post3',
   'pre1post4',
   'pre2post0',
   'pre2post1',
   'pre2post2',
   'pre2post3',
   'pre2post4',
   'pre3post0',
   'pre3post1',
   'pre3post2',
   'pre3post3',
   'pre3post4',
   'pre4post0',
   'pre4post1',
   'pre4post2',
   'pre4post3',
   'pre4post4'),
  ('nan', 3)),
 ('allEqual',
  'p2',
  ('pre0post0',
   'pre0post1',
   'pre0post2',
   'pre0post3',
   'pre0post4',
   'pre1post0',
   'pre1post1',
   'pre1post2',
   'pre1post3',
   'pre1post4',
   'pre2post0',
   'pre2post1',
   'pre2post2',
   'pre2post3',
   'pre2post4',
   'pre3post0',
   'pre3post1',
   'pre3post2',
   'pre3post3',
   'pre3post4',
   'pre4post0',
   'pre4post1',
   'pre4post2',
   'pre4post3',
   'pre4post4'),
  ('nan', 3)),
 ('allEqual',
  'p3',
  ('pre0post0',
   'pre0post1',
   'pre0post2',
   'pre0post3',
   'pre0post4',
   'pre1post0',
   'pre1post1',
   'pre1post2',
   'pre1post3',
   'pre1post4',
   'pre2post0',
   'pre2post1',
   'pre2post2',
   'pre2post3',
   'pre2post4',
   'pre3post0',
   'pre3post1',
   'pre3post2',
   'pre3post3',
   'pre3post4',
   'pre4post0',
   'pre4post1',
   'pre4post2',
   'pre4post3',
   'pre4post4'),
  ('nan', 3)),
 ('allEqual',
  'p4',
  ('pre0post0',
   'pre0post1',
   'pre0post2',
   'pre0post3',
   'pre0post4',
   'pre1post0',
   'pre1post1',
   'pre1post2',
   'pre1post3',
   'pre1post4',
   'pre2post0',
   'pre2post1',
   'pre2post2',
   'pre2post3',
   'pre2post4',
   'pre3post0',
   'pre3post1',
   'pre3post2',
   'pre3post3',
   'pre3post4',
   'pre4post0',
   'pre4post1',
   'pre4post2',
   'pre4post3',
   'pre4post4'),
  ('nan', 3)),
 ('allEqual',
  'p5',
  ('pre0post0',
   'pre0post1',
   'pre0post2',
   'pre0post3',
   'pre0post4',
   'pre1post0',
   'pre1post1',
   'pre1post2',
   'pre1post3',
   'pre1post4',
   'pre2post0',
   'pre2post1',
   'pre2post2',
   'pre2post3',
   'pre2post4',
   'pre3post0',
   'pre3post1',
   'pre3post2',
   'pre3post3',
   'pre3post4',
   'pre4post0',
   'pre4post1',
   'pre4post2',
   'pre4post3',
   'pre4post4'),
  ('nan', 3)),
 ('allEqual',
  'p6',
  ('pre0post0',
   'pre0post1',
   'pre0post2',
   'pre0post3',
   'pre0post4',
   'pre1post0',
   'pre1post1',
   'pre1post2',
   'pre1post3',
   'pre1post4',
   'pre2post0',
   'pre2post1',
   'pre2post2',
   'pre2post3',
   'pre2post4',
   'pre3post0',
   'pre3post1',
   'pre3post2',
   'pre3post3',
   'pre3post4',
   'pre4post0',
   'pre4post1',
   'pre4post2',
   'pre4post3',
   'pre4post4'),
  ('nan', 3)),
 ('allEqual',
  'p7',
  ('pre0post0',
   'pre0post1',
   'pre0post2',
   'pre0post3',
   'pre0post4',
   'pre1post0',
   'pre1post1',
   'pre1post2',
   'pre1post3',
   'pre1post4',
   'pre2post0',
   'pre2post1',
   'pre2post2',
   'pre2post3',
   'pre2post4',
   'pre3post0',
   'pre3post1',
   'pre3post2',
   'pre3post3',
   'pre3post4',
   'pre4post0',
   'pre4post1',
   'pre4post2',
   'pre4post3',
   'pre4post4'),
  ('nan', 3)),
 ('allEqual',
  'p8',
  ('pre0post0',
   'pre0post1',
   'pre0post2',
   'pre0post3',
   'pre0post4',
   'pre1post0',
   'pre1post1',
   'pre1post2',
   'pre1post3',
   'pre1post4',
   'pre2post0',
   'pre2post1',
   'pre2post2',
   'pre2post3',
   'pre2post4',
   'pre3post0',
   'pre3post1',
   'pre3post2',
   'pre3post3',
   'pre3post4',
   'pre4post0',
   'pre4post1',
   'pre4post2',
   'pre4post3',
   'pre4post4'),
  ('nan', 3)),
 ('allEqual',
  'p9',
  ('pre0post0',
   'pre0post1',
   'pre0post2',
   'pre0post3',
   'pre0post4',
   'pre1post0',
   'pre1post1',
   'pre1post2',
   'pre1post3',
   'pre1post4',
   'pre2post0',
   'pre2post1',
   'pre2post2',
   'pre2post3',
   'pre2post4',
   'pre3post0',
   'pre3post1',
   'pre3post2',
   'pre3post3',
   'pre3post4',
   'pre4post0',
   'pre4post1',
   'pre4post2',
   'pre4post3',
   'pre4post4'),
  ('nan', 3)),
 ('allEqual',
  'p10',
  ('pre0post0',
   'pre0post1',
   'pre0post2',
   'pre0post3',
   'pre0post4',
   'pre1post0',
   'pre1post1',
   'pre1post2',
   'pre1post3',
   'pre1post4',
   'pre2post0',
   'pre2post1',
   'pre2post2',
   'pre2post3',
   'pre2post4',
   'pre3post0',
   'pre3post1',
   'pre3post2',
   'pre3post3',
   'pre3post4',
   'pre4post0',
   'pre4post1',
   'pre4post2',
   'pre4post3',
   'pre4post4'),
  ('nan', 3)),
 ('allEqual',
  'p11',
  ('pre0post0',
   'pre0post1',
   'pre0post2',
   'pre0post3',
   'pre0post4',
   'pre1post0',
   'pre1post1',
   'pre1post2',
   'pre1post3',
   'pre1post4',
   'pre2post0',
   'pre2post1',
   'pre2post2',
   'pre2post3',
   'pre2post4',
   'pre3post0',
   'pre3post1',
   'pre3post2',
   'pre3post3',
   'pre3post4',
   'pre4post0',
   'pre4post1',
   'pre4post2',
   'pre4post3',
   'pre4post4'),
  ('+zero', 'nan')),
 ('tiny',
  'p5',
  ('pre0post0',
   'pre0post1',
   'pre0post2',
   'pre0post3',
   'pre0post4',
   'pre1post0',
   'pre1post1',
   'pre1post2',
   'pre1post3',
   'pre1post4',
   'pre2post0',
   'pre2post1',
   'pre2post2',
   'pre2post3',
   'pre2post4',
   'pre3post0',
   'pre3post1',
   'pre3post2',
   'pre3post3',
   'pre3post4',
   'pre4post0',
   'pre4post1',
   'pre4post2',
   'pre4post3',
   'pre4post4'),
  (7.3125, 7)),
 ('tiny',
  'p6',
  ('pre0post0',
   'pre0post1',
   'pre0post2',
   'pre0post3',
   'pre0post4',
   'pre1post0',
   'pre1post1',
   'pre1post2',
   'pre1post3',
   'pre1post4',
   'pre2post0',
   'pre2post1',
   'pre2post2',
   'pre2post3',
   'pre2post4',
   'pre3post0',
   'pre3post1',
   'pre3post2',
   'pre3post3',
   'pre3post4',
   'pre4post0',
   'pre4post1',
   'pre4post2',
   'pre4post3',
   'pre4post4'),
  (8, 7)),
 ('tiny',
  'p7',
  ('pre0post0',
   'pre0post1',
   'pre0post2',
   'pre0post3',
   'pre0post4',
   'pre1post0',
   'pre1post1',
   'pre1post2',
   'pre1post3',
   'pre1post4',
   'pre2post0',
   'pre2post1',
   'pre2post2',
   'pre2post3',
   'pre2post4',
   'pre3post0',
   'pre3post1',
   'pre3post2',
   'pre3post3',
   'pre3post4',
   'pre4post0',
   'pre4post1',
   'pre4post2',
   'pre4post3',
   'pre4post4'),
  (9, 7)),
 ('tiny',
  'p11',
  ('pre0post0',
   'pre0post1',
   'pre0post2',
   'pre0post3',
   'pre0post4',
   'pre1post0',
   'pre1post1',
   'pre1post2',
   'pre1post3',
   'pre1post4',
   'pre2post0',
   'pre2post1',
   'pre2post2',
   'pre2post3',
   'pre2post4',
   'pre3post0',
   'pre3post1',
   'pre3post2',
   'pre3post3',
   'pre3post4',
   'pre4post0',
   'pre4post1',
   'pre4post2',
   'pre4post3',
   'pre4post4'),
  ('+zero', 'nan')),
 ('tiny',
  'p8',
  ('pre0post1',
   'pre0post3',
   'pre1post1',
   'pre1post3',
   'pre2post1',
   'pre2post3',
   'pre3post1',
   'pre3post3',
   'pre4post1',
   'pre4post3'),
  (8, 7)),
 ('tiny', 'p8', ('pre0post2', 'pre1post2', 'pre2post2', 'pre3post2', 'pre4post2'), (10, 9)),
 ('tiny', 'p10', ('pre0post3', 'pre1post3', 'pre2post3', 'pre3post3', 'pre4post3'), ('+zero', 9)),
 ('tiny', 'p0', ('pre1post0', 'pre1post1', 'pre1post2', 'pre1post3', 'pre1post4'), (9, 7)),
 ('tiny',
  'p1',
  ('pre1post0',
   'pre1post1',
   'pre1post2',
   'pre1post3',
   'pre1post4',
   'pre3post0',
   'pre3post1',
   'pre3post2',
   'pre3post3',
   'pre3post4'),
  (9, 7)),
 ('tiny',
  'p2',
  ('pre1post0',
   'pre1post1',
   'pre1post2',
   'pre1post3',
   'pre1post4',
   'pre3post0',
   'pre3post1',
   'pre3post2',
   'pre3post3',
   'pre3post4'),
  (8, 7)),
 ('tiny', 'p0', ('pre2post0', 'pre2post1', 'pre2post2', 'pre2post3', 'pre2post4'), (3, 1)),
 ('tiny', 'p1', ('pre2post0', 'pre2post1', 'pre2post2', 'pre2post3', 'pre2post4'), (5, 3)),
 ('tiny', 'p2', ('pre2post0', 'pre2post1', 'pre2post2', 'pre2post3', 'pre2post4'), (6, 5)),
 ('epsilon',
  'p11',
  ('pre0post0',
   'pre0post1',
   'pre0post2',
   'pre0post3',
   'pre0post4',
   'pre1post0',
   'pre1post1',
   'pre1post2',
   'pre1post3',
   'pre1post4',
   'pre2post0',
   'pre2post1',
   'pre2post2',
   'pre2post3',
   'pre2post4',
   'pre3post0',
   'pre3post1',
   'pre3post2',
   'pre3post3',
   'pre3post4',
   'pre4post0',
   'pre4post1',
   'pre4post2',
   'pre4post3',
   'pre4post4'),
  ('+zero', 'nan')),
 ('seeded0',
  'p11',
  ('pre0post0',
   'pre0post1',
   'pre0post2',
   'pre0post3',
   'pre0post4',
   'pre1post0',
   'pre1post1',
   'pre1post2',
   'pre1post3',
   'pre1post4',
   'pre2post0',
   'pre2post1',
   'pre2post2',
   'pre2post3',
   'pre2post4',
   'pre3post0',
   'pre3post1',
   'pre3post2',
   'pre3post3',
   'pre3post4',
   'pre4post0',
   'pre4post1',
   'pre4post2',
   'pre4post3',
   'pre4post4'),
  ('+zero', 'nan')),
 ('seeded0', 'p8', ('pre0post4', 'pre1post4', 'pre2post4', 'pre3post4', 'pre4post4'), (16.375, 4.5625)),
 ('seeded0', 'p9', ('pre0post4', 'pre1post4', 'pre2post4', 'pre3post4', 'pre4post4'), (32.875, 9.25)),
 ('seeded0', 'p10', ('pre0post4', 'pre1post4', 'pre2post4', 'pre3post4', 'pre4post4'), (65.875, 18.625)),
 ('seeded1',
  'p11',
  ('pre0post0',
   'pre0post1',
   'pre0post2',
   'pre0post3',
   'pre0post4',
   'pre1post0',
   'pre1post1',
   'pre1post2',
   'pre1post3',
   'pre1post4',
   'pre2post0',
   'pre2post1',
   'pre2post2',
   'pre2post3',
   'pre2post4',
   'pre3post0',
   'pre3post1',
   'pre3post2',
   'pre3post3',
   'pre3post4',
   'pre4post0',
   'pre4post1',
   'pre4post2',
   'pre4post3',
   'pre4post4'),
  ('+zero', 'nan')),
 ('seeded1', 'p8', ('pre0post4', 'pre1post4', 'pre2post4', 'pre3post4', 'pre4post4'), (9.1875, -7.875)),
 ('seeded1', 'p9', ('pre0post4', 'pre1post4', 'pre2post4', 'pre3post4', 'pre4post4'), (25.125, -9)),
 ('seeded1', 'p10', ('pre0post4', 'pre1post4', 'pre2post4', 'pre3post4', 'pre4post4'), (57, -11.25)),
 ('seeded2',
  'p11',
  ('pre0post0',
   'pre0post1',
   'pre0post2',
   'pre0post3',
   'pre0post4',
   'pre1post0',
   'pre1post1',
   'pre1post2',
   'pre1post3',
   'pre1post4',
   'pre2post0',
   'pre2post1',
   'pre2post2',
   'pre2post3',
   'pre2post4',
   'pre3post0',
   'pre3post1',
   'pre3post2',
   'pre3post3',
   'pre3post4',
   'pre4post0',
   'pre4post1',
   'pre4post2',
   'pre4post3',
   'pre4post4'),
  ('+zero', 'nan')),
 ('seeded2', 'p8', ('pre0post4', 'pre1post4', 'pre2post4', 'pre3post4', 'pre4post4'), (-16.1875, 11.9375)),
 ('seeded2', 'p9', ('pre0post4', 'pre1post4', 'pre2post4', 'pre3post4', 'pre4post4'), (-25.375, 30.875)),
 ('seeded2', 'p10', ('pre0post4', 'pre1post4', 'pre2post4', 'pre3post4', 'pre4post4'), (-43.75, 68.75)),
 ('seeded3',
  'p11',
  ('pre0post0',
   'pre0post1',
   'pre0post2',
   'pre0post3',
   'pre0post4',
   'pre1post0',
   'pre1post1',
   'pre1post2',
   'pre1post3',
   'pre1post4',
   'pre2post0',
   'pre2post1',
   'pre2post2',
   'pre2post3',
   'pre2post4',
   'pre3post0',
   'pre3post1',
   'pre3post2',
   'pre3post3',
   'pre3post4',
   'pre4post0',
   'pre4post1',
   'pre4post2',
   'pre4post3',
   'pre4post4'),
  ('+zero', 'nan')),
 ('seeded3', 'p8', ('pre0post4', 'pre1post4', 'pre2post4', 'pre3post4', 'pre4post4'), (27.125, 6.125)),
 ('seeded3', 'p9', ('pre0post4', 'pre1post4', 'pre2post4', 'pre3post4', 'pre4post4'), (48.125, 6.125)),
 ('seeded3', 'p10', ('pre0post4', 'pre1post4', 'pre2post4', 'pre3post4', 'pre4post4'), (90.125, 6.125))]
    return {f"/CurveEdges/cases/{case}/evaluations/{loop}/values/{query}/value": pair
            for case, query, loops, pair in witnesses for loop in loops}


# Local XNA assembly inspection: xna-curve/README.md identifies the DLL, version, token,
# SHA and regeneration command. CurveKeyCollection.Add IL_0020..IL_005a advances past equal
# keys; Curve.ComputeTangent IL_010e..IL_011f checks the value delta against 2^-23.
# No disassembly is distributed or loaded by this audit.
CURVE_IL_RETAINED = {'/CurveEdges/cases/duplicateStart/insertionKeys/k0/value': (10, '+zero'),
 '/CurveEdges/cases/duplicateStart/insertionKeys/k1/value': ('+zero', 10),
 '/CurveEdges/cases/allEqual/insertionKeys/k0/value': (9, 3),
 '/CurveEdges/cases/allEqual/insertionKeys/k0/in': (4, 1),
 '/CurveEdges/cases/allEqual/insertionKeys/k0/out': (5, 2),
 '/CurveEdges/cases/allEqual/insertionKeys/k1/value': (3, 9),
 '/CurveEdges/cases/allEqual/insertionKeys/k1/in': (1, 4),
 '/CurveEdges/cases/allEqual/insertionKeys/k1/out': (2, 5),
 '/CurveEdges/cases/tiny/tangents/in0out2/both/k0/out': ('+zero', 2),
 '/CurveEdges/cases/tiny/tangents/in0out2/each/k0/out': ('+zero', 2),
 '/CurveEdges/cases/tiny/tangents/in1out2/both/k0/out': ('+zero', 2),
 '/CurveEdges/cases/tiny/tangents/in1out2/each/k0/out': ('+zero', 2),
 '/CurveEdges/cases/tiny/tangents/in2out0/single/k0/out': ('+zero', 2),
 '/CurveEdges/cases/tiny/tangents/in2out0/singleEach/k0/out': ('+zero', 2),
 '/CurveEdges/cases/tiny/tangents/in2out1/single/k0/out': ('+zero', 2),
 '/CurveEdges/cases/tiny/tangents/in2out1/singleEach/k0/out': ('+zero', 2),
 '/CurveEdges/cases/tiny/tangents/in2out2/both/k0/out': ('+zero', 2),
 '/CurveEdges/cases/tiny/tangents/in2out2/each/k0/out': ('+zero', 2),
 '/CurveEdges/cases/tiny/tangents/in2out2/single/k0/out': ('+zero', 2),
 '/CurveEdges/cases/tiny/tangents/in2out2/singleEach/k0/out': ('+zero', 2)}


def retained_differences():
    """Exact witnesses, not wildcard paths or relaxed numeric tolerances."""
    expected = {}
    for dimension in (2, 3, 4):
        prefix = f"/Vector{dimension}Edges/magnitude"
        expected[prefix + "/large/length"] = ("+inf", single(1e20))
        expected[prefix + "/tiny/length"] = ("+zero", single(1e-30))
        for overload in ("normalize", "normalizeOut", "normalizeInstance"):
            expected[f"{prefix}/large/{overload}/X"] = ("+zero", 1)
            expected[f"{prefix}/tiny/{overload}/X"] = ("+inf", 1)
            for component in "YZW"[:dimension - 1]:
                expected[f"{prefix}/tiny/{overload}/{component}"] = ("nan", "+zero")
    # Existing XNA wide-product/subtraction-order decision 0e7be08e0.
    for case in ("zero", "wideCancellation"):
        for overload in ("cross", "crossOut"):
            expected[f"/Vector3Edges/products/{case}/{overload}/Y"] = ("-zero", "+zero")
            if case == "wideCancellation":
                expected[f"/Vector3Edges/products/{case}/{overload}/Z"] = ("nan", "+zero")
    for kind in ("size", "fov", "offCenter"):
        cases = ["nearZero", "farZero", "equalDepth", "reversedDepth", "bothZero"]
        if kind == "fov":
            cases += ["zeroDimension", "negativeDimension", "fnaPi", "infiniteAngle"]
        for case in cases:
            for overload in ("valueException", "outException"):
                expected[f"/MatrixPerspectiveEdges/{kind}/{case}/{overload}"] = (
                    "ArgumentException", "ArgumentOutOfRangeException")
    prefix = "/MatrixPerspectiveEdges/fov/pi"
    # Measured FNA/net8 coefficient at the public Single Pi; XNA refuses this angle.
    projection = matrix({"M11": -4.371139183945161e-08, "M22": -4.371139183945161e-08,
                         "M33": -2, "M34": -1, "M43": -2})
    sentinel = matrix({"M11": 1, "M22": 1, "M33": 1, "M41": 7, "M42": 8, "M43": 9, "M44": 1})
    expected[prefix + "/value"] = (projection, "<missing on CNA side>")
    expected[prefix + "/valueException"] = ("none", "ArgumentOutOfRangeException")
    expected[prefix + "/outException"] = ("none", "ArgumentOutOfRangeException")
    for field in projection:
        if projection[field] != sentinel[field]:
            expected[prefix + "/out/" + field] = (projection[field], sentinel[field])
    expected.update(quaternion_retained_differences())
    expected.update(matrix_retained_differences())
    expected.update(geometry_retained_differences())
    expected.update(packing_retained_differences())
    expected.update(half_retained_differences())
    expected.update(curve_retained_differences())
    expected.update(CURVE_IL_RETAINED)
    return expected


def require_keys(value, names):
    if not isinstance(value, dict) or set(value) != set(names):
        raise ValueError(f"incomplete edge witness shape; expected keys {sorted(names)}")


def validate_vector_shape(vector, dimension):
    extra = () if dimension == 2 else ("spatial",)
    if dimension == 3:
        extra += ("products",)
    require_keys(vector, ("seed", "arithmetic", "magnitude", "format", "unequalEquals",
                         "unequalOperator", "unequalNotOperator", "signedZeroEquals", "signedZeroHashEqual") + extra)
    cases = ["zero", "signedZero", "boundaries", "outside", "infinity", "nan", "max", "subnormal"]
    require_keys(vector["arithmetic"], cases + [f"seeded{i}" for i in range(8)])
    operations = ("input", "divisor", "divide", "divideOut", "divideOperator", "scalarDivide",
                  "scalarDivideOut", "scalarDivideOperator", "clamp", "clampOut", "reversedClamp", "reversedClampOut")
    for case in vector["arithmetic"].values():
        require_keys(case, operations + ("equalsCopy", "operatorEqualCopy", "operatorUnequalCopy", "copyHashEqual"))
        for operation in operations:
            require_keys(case[operation], "XYZW"[:dimension])
    require_keys(vector["magnitude"], ("zero", "threeFour", "large", "tiny", "infinity", "nan"))
    for case in vector["magnitude"].values():
        require_keys(case, ("input", "length", "lengthSquared", "normalize", "normalizeOut", "normalizeInstance"))
        for operation in ("input", "normalize", "normalizeOut", "normalizeInstance"):
            require_keys(case[operation], "XYZW"[:dimension])

    if dimension > 2:
        require_keys(vector["spatial"], [f"fixed{i}" for i in range(8)] + [f"seeded{i}" for i in range(4)])
        operations = ["lerp", "smoothStep", "hermite", "catmullRom", "barycentric", "transform", "rotate"]
        operations += ["cross", "reflect", "normal"] if dimension == 3 else ["transform2", "rotate2", "transform3", "rotate3"]
        outputs = operations + [name + "Out" for name in operations]
        for case in vector["spatial"].values():
            require_keys(case, ["a", "b", "c", "d", "amount"] + outputs)
            for field in ["a", "b", "c", "d"] + outputs:
                require_keys(case[field], "XYZW"[:dimension])
    if dimension == 3:
        require_keys(vector["products"], ("zero", "wideCancellation"))
        for case in vector["products"].values():
            require_keys(case, ("a", "b", "cross", "crossOut"))
            for value in case.values():
                require_keys(value, "XYZ")


def validate_quaternion_shape(rotation):
    require_keys(rotation, ("seed", "unary", "pairs", "factories", "matrices"))
    require_keys(rotation["unary"], ["zero", "identity", "nonunit", "large", "tiny", "nan", "infinity"] +
                 [f"seeded{i}" for i in range(4)])
    fields = ["input", "normalize", "normalizeOut", "normalizeInstance", "inverse", "inverseOut",
              "conjugate", "conjugateOut", "conjugateInstance"]
    for case in rotation["unary"].values():
        require_keys(case, fields + ["length", "lengthSquared"])
        for field in fields:
            require_keys(case[field], "XYZW")
    require_keys(rotation["pairs"], ["identical", "opposite", "zero", "nearThreshold"] +
                 [f"orthogonal{i}" for i in range(7)] + [f"seeded{i}" for i in range(4)])
    fields = ["a", "b", "lerp", "lerpOut", "slerp", "slerpOut", "concatenate", "concatenateOut"]
    for case in rotation["pairs"].values():
        require_keys(case, fields + ["amount"])
        for field in fields:
            require_keys(case[field], "XYZW")
    require_keys(rotation["factories"], [f"angle{i}" for i in range(6)] + ["zeroAxis", "nonunitAxis"] +
                 [f"seeded{i}" for i in range(4)])
    fields = ["axisAngle", "axisAngleOut", "yawPitchRoll", "yawPitchRollOut"]
    for case in rotation["factories"].values():
        require_keys(case, fields + ["axis", "angle", "pitch", "roll"])
        require_keys(case["axis"], "XYZ")
        for field in fields:
            require_keys(case[field], "XYZW")
    require_keys(rotation["matrices"], ("identity", "xHalfTurn", "yHalfTurn", "zHalfTurn", "zero"))
    for case in rotation["matrices"].values():
        require_keys(case, ("input", "value", "out"))
        require_keys(case["input"], matrix({}))
        require_keys(case["value"], "XYZW")
        require_keys(case["out"], "XYZW")


def validate_matrix_shape(value):
    require_keys(value, ("seed", "cameras", "rotations", "planes", "algebra", "decomposition"))
    require_keys(value["cameras"], ["ordinary", "coincident", "explicitForward", "nonunitForward", "zeroUp",
                 "parallelUp", "parallelForwards", "forwardAxis", "belowDistance", "aboveDistance",
                 "belowParallel", "aboveParallel", "large", "tiny", "nan", "infinity"] +
                 [f"seeded{i}" for i in range(4)])
    outputs = ["lookAt", "world", "billboard", "constrained"]
    outputs += [name + "Out" for name in outputs]
    vectors = ["position", "target", "up", "cameraForward", "objectForward"]
    for case in value["cameras"].values():
        require_keys(case, outputs + vectors + ["hasCameraForward", "hasObjectForward"])
        for field in vectors:
            require_keys(case[field], "XYZ")
        for field in outputs:
            require_keys(case[field], matrix({}))
    require_keys(value["rotations"], [f"angle{i}" for i in range(6)] + ["zero", "nonunit", "large", "tiny"] +
                 [f"seeded{i}" for i in range(8)])
    outputs = ["axisAngle", "axisAngleOut", "quaternionValue", "quaternionOut", "yawPitchRoll", "yawPitchRollOut"]
    for case in value["rotations"].values():
        require_keys(case, outputs + ["axis", "quaternion", "angle", "pitch", "roll"])
        require_keys(case["axis"], "XYZ")
        require_keys(case["quaternion"], "XYZW")
        for field in outputs:
            require_keys(case[field], matrix({}))
    require_keys(value["planes"], ["unit", "nonunit", "zero", "nearUnit", "large", "tiny", "nan", "infinity",
                 "parallelLight", "zeroLight"] + [f"seeded{i}" for i in range(4)])
    outputs = ["reflection", "reflectionOut", "shadow", "shadowOut"]
    for case in value["planes"].values():
        require_keys(case, outputs + ["normal", "light", "d"])
        require_keys(case["normal"], "XYZ")
        require_keys(case["light"], "XYZ")
        for field in outputs:
            require_keys(case[field], matrix({}))
    require_keys(value["algebra"], ["identity", "zero", "singular", "tiny", "large", "sequential", "nearSingular", "ieee"] +
                 [f"amount{i}" for i in range(6)] + [f"seeded{i}" for i in range(4)])
    outputs = ["input", "other", "inverse", "inverseOut", "inverseAliased", "transpose", "transposeOut",
               "transposeAliased", "lerp", "lerpOut", "lerpAliasedFirst", "lerpAliasedSecond"]
    for case in value["algebra"].values():
        require_keys(case, outputs + ["amount", "determinant"])
        for field in outputs:
            require_keys(case[field], matrix({}))
    require_keys(value["decomposition"], ["unit", "negative", "negativeTie", "twoNegative", "zeroAxis", "allZero",
                 "belowEpsilon", "atEpsilon", "large", "nan", "infinity", "shear", "fourthColumn"] +
                 [f"seeded{i}" for i in range(4)])
    for case in value["decomposition"].values():
        require_keys(case, ("input", "accepted", "exception", "scale", "rotation", "translation"))
        require_keys(case["input"], matrix({}))
        require_keys(case["scale"], "XYZ")
        require_keys(case["translation"], "XYZ")
        require_keys(case["rotation"], "XYZW")


def validate_geometry_shape(value):
    require_keys(value, ("seed", "planes", "relations", "pointSets", "frustums"))
    def plane(item):
        require_keys(item, ("normal", "d"))
        require_keys(item["normal"], "XYZ")
    def box(item):
        require_keys(item, ("min", "max"))
        require_keys(item["min"], "XYZ")
        require_keys(item["max"], "XYZ")
    def sphere(item):
        require_keys(item, ("center", "radius"))
        require_keys(item["center"], "XYZ")
    plane_names = ["unit", "scaled", "zero", "nearUnit", "large", "tiny", "nan", "infinity", "singular"]
    require_keys(value["planes"], plane_names + [f"seeded{i}" for i in range(4)])
    outputs = ["normalize", "normalizeOut", "normalizeInstance", "normalizeAliased", "matrixTransform",
               "matrixTransformOut", "matrixTransformAliased", "quaternionTransform", "quaternionTransformOut",
               "quaternionTransformAliased"]
    for case in value["planes"].values():
        require_keys(case, outputs + ["input", "point", "matrix", "quaternion", "dot", "dotOut", "dotCoordinate",
                                     "dotCoordinateOut", "dotNormal", "dotNormalOut"])
        for field in outputs + ["input"]:
            plane(case[field])
        require_keys(case["point"], "XYZ")
        require_keys(case["matrix"], matrix({}))
        require_keys(case["quaternion"], "XYZW")
    require_keys(value["relations"], ["inside", "boundary", "disjoint", "zero", "insideOut", "negativeRadius",
                 "stationary", "parallelOutside"] + [f"threshold{i}" for i in range(6)] +
                 [f"seeded{i}" for i in range(6)])
    fields = ['box', 'sphere', 'origin', 'direction', 'plane', 'point', 'boxContainsBox', 'boxContainsBoxOut',
 'boxContainsSphere', 'boxContainsSphereOut', 'boxContainsPoint', 'boxContainsPointOut',
 'boxContainsFrustum', 'sphereContainsBox', 'sphereContainsBoxOut', 'sphereContainsSphere',
 'sphereContainsSphereOut', 'sphereContainsPoint', 'sphereContainsPointOut', 'sphereContainsFrustum',
 'frustumContainsBox', 'frustumContainsBoxOut', 'frustumContainsSphere', 'frustumContainsSphereOut',
 'frustumContainsPoint', 'frustumContainsPointOut', 'boxIntersectsBox', 'boxIntersectsBoxOut',
 'boxIntersectsSphere', 'boxIntersectsSphereOut', 'boxIntersectsPlane', 'boxIntersectsPlaneOut',
 'boxIntersectsRay', 'boxIntersectsRayOut', 'boxIntersectsFrustum', 'sphereIntersectsBox',
 'sphereIntersectsBoxOut', 'sphereIntersectsSphere', 'sphereIntersectsSphereOut',
 'sphereIntersectsPlane', 'sphereIntersectsPlaneOut', 'sphereIntersectsRay', 'sphereIntersectsRayOut',
 'sphereIntersectsFrustum', 'rayIntersectsBox', 'rayIntersectsBoxOut', 'rayIntersectsSphere',
 'rayIntersectsSphereOut', 'rayIntersectsPlane', 'rayIntersectsPlaneOut', 'planeIntersectsBox',
 'planeIntersectsBoxOut', 'planeIntersectsSphere', 'planeIntersectsSphereOut', 'planeIntersectsFrustum',
 'frustumIntersectsBox', 'frustumIntersectsBoxOut', 'frustumIntersectsSphere',
 'frustumIntersectsSphereOut', 'frustumIntersectsPlane', 'frustumIntersectsPlaneOut', 'mergedBox',
 'mergedBoxOut', 'boxFromSphere', 'boxFromSphereOut', 'sphereFromBox', 'sphereFromBoxOut',
 'mergedSphere', 'mergedSphereOut']
    for case in value["relations"].values():
        require_keys(case, fields)
        for field in ("box", "mergedBox", "mergedBoxOut", "boxFromSphere", "boxFromSphereOut"):
            box(case[field])
        for field in ("sphere", "sphereFromBox", "sphereFromBoxOut", "mergedSphere", "mergedSphereOut"):
            sphere(case[field])
        plane(case["plane"])
        for field in ("origin", "direction", "point"):
            require_keys(case[field], "XYZ")
    point_counts = {"empty": 0, "single": 1, "repeated": 3, "pair": 2, "triangleTie": 3,
                    "largePair": 2, "tinyPair": 2, **{f"seeded{i}": 5 for i in range(4)}}
    require_keys(value["pointSets"], point_counts)
    for name, count in point_counts.items():
        case = value["pointSets"][name]
        fields = ["points", "count", "boxException", "sphereException"]
        require_keys(case, fields + (["box", "sphere"] if count else []))
        require_keys(case["points"], [f"p{i}" for i in range(count)])
        if type(case["count"]) is not int or case["count"] != count:
            raise ValueError("incomplete edge witness shape; point count changed")
        for item in case["points"].values():
            require_keys(item, "XYZ")
        if count:
            box(case["box"])
            sphere(case["sphere"])
    require_keys(value["frustums"], ("identity", "touching", "separated", "enclosing", "enclosed"))
    for case in value["frustums"].values():
        require_keys(case, ("matrix", "contains", "reverseContains", "intersects", "reverseIntersects", "corners",
                            "cornersOut", "sphere"))
        require_keys(case["matrix"], matrix({}))
        sphere(case["sphere"])
        for field in ("corners", "cornersOut"):
            require_keys(case[field], [f"p{i}" for i in range(8)])
            for point in case[field].values():
                require_keys(point, "XYZ")


def validate_packing_shape(value):
    require_keys(value, ("seed", "packedTypes", "colors", "integers", "interpolation"))
    types = ['Alpha8', 'Bgr565', 'Bgra4444', 'Bgra5551', 'Byte4', 'HalfSingle', 'HalfVector2', 'HalfVector4', 'NormalizedByte2', 'NormalizedByte4', 'NormalizedShort2', 'NormalizedShort4', 'Rg32', 'Rgba1010102', 'Rgba64', 'Short2', 'Short4']
    names = ["zero", "signedZero", "one", "negative", "quarters", "ties", "belowTies", "aboveTies",
             "clampLow", "clampHigh", "byteBoundary", "shortBoundary", "halfBoundary", "halfTiny", "nonfinite"]
    names += [f"seeded{i}" for i in range(4)]
    require_keys(value["packedTypes"], types)
    for cases in value["packedTypes"].values():
        require_keys(cases, names)
        for case in cases.values():
            require_keys(case, ("input", "constructor", "vectorConstructor", "unpack", "vectorUnpack", "pack",
                                "packUnpack", "raw", "rawUnpack", "repackedRaw"))
            for field in ("input", "unpack", "vectorUnpack", "packUnpack", "rawUnpack"):
                require_keys(case[field], "XYZW")
            for field in ("constructor", "vectorConstructor", "pack", "raw", "repackedRaw"):
                text = case[field]
                if not isinstance(text, str) or len(text) != 16 or any(c not in "0123456789abcdef" for c in text):
                    raise ValueError("incomplete edge witness shape; packed value needs 16 lowercase hex digits")
    def color(item):
        require_keys(item, ("packed", "r", "g", "b", "a", "vector3", "vector4"))
        require_keys(item["vector3"], "XYZ")
        require_keys(item["vector4"], "XYZW")
    require_keys(value["colors"], names)
    for case in value["colors"].values():
        outputs = ["vector4", "floats4", "vector3", "floats3", "pack", "raw", "nonpremultiplied"]
        require_keys(case, outputs + ["input"])
        require_keys(case["input"], "XYZW")
        for field in outputs:
            color(case[field])
    require_keys(value["integers"], [f"case{i}" for i in range(6)])
    for case in value["integers"].values():
        require_keys(case, ("r", "g", "b", "a", "rgb", "rgba", "nonpremultiplied"))
        for field in ("rgb", "rgba", "nonpremultiplied"):
            color(case[field])
    require_keys(value["interpolation"], [f"case{i}" for i in range(18)])
    for case in value["interpolation"].values():
        require_keys(case, ("amount", "lerp", "multiply", "operator"))
        for field in ("lerp", "multiply", "operator"):
            color(case[field])


def validate_curve_shape(value):
    require_keys(value, ("seed", "cases"))
    counts = {"empty": 0, "single": 1, "smooth": 3, "stepShifted": 2, "duplicateStart": 3,
              "allEqual": 2, "tiny": 2, "epsilon": 2, **{f"seeded{i}": 3 for i in range(4)}}
    require_keys(value["cases"], counts)
    def keys(items, count):
        require_keys(items, [f"k{i}" for i in range(count)])
        for key in items.values():
            require_keys(key, ("position", "value", "in", "out", "continuity"))
    for name, count in counts.items():
        case = value["cases"][name]
        require_keys(case, ("keys", "insertionKeys", "count", "constant", "defaultPre", "defaultPost",
                            "negativeIndex", "endIndex", "evaluations", "tangents"))
        if type(case["count"]) is not int or case["count"] != count:
            raise ValueError("incomplete edge witness shape; curve key count changed")
        keys(case["keys"], count)
        keys(case["insertionKeys"], count)
        require_keys(case["evaluations"], [f"pre{i}post{j}" for i in range(5) for j in range(5)])
        for entry in case["evaluations"].values():
            require_keys(entry, ("pre", "post", "values"))
            require_keys(entry["values"], [f"p{i}" for i in range(12)])
            for point in entry["values"].values():
                require_keys(point, ("position", "value"))
        require_keys(case["tangents"], [f"in{i}out{j}" for i in range(3) for j in range(3)])
        for entry in case["tangents"].values():
            require_keys(entry, ("both", "each", "single", "singleEach"))
            for result in entry.values():
                keys(result, count)


def validate_witness_shape(document, is_reference):
    validate_curve_shape(document["CurveEdges"])
    validate_packing_shape(document["PackingEdges"])
    validate_geometry_shape(document["GeometryEdges"])
    validate_matrix_shape(document["MatrixEdges"])
    validate_quaternion_shape(document["QuaternionEdges"])
    for dimension in (2, 3, 4):
        validate_vector_shape(document[f"Vector{dimension}Edges"], dimension)
    projection = document["MatrixPerspectiveEdges"]
    require_keys(projection, ("seed", "size", "fov", "offCenter"))
    refused = {"nearZero", "farZero", "equalDepth", "reversedDepth", "bothZero"}
    cases = refused | {"ordinary", "zeroDimension", "negativeDimension", "nanNear", "nanFar",
                       "infiniteFar", "nanDimension"} | {f"seeded{i}" for i in range(4)}
    for kind in ("size", "fov", "offCenter"):
        names = cases | {"pi", "belowPi", "fnaPi", "infiniteAngle"} if kind == "fov" else cases
        require_keys(projection[kind], names)
        for name, case in projection[kind].items():
            fails = name in refused or (kind == "fov" and name in {
                "zeroDimension", "negativeDimension", "fnaPi", "infiniteAngle"})
            fails = fails or (kind == "fov" and name == "pi" and not is_reference)
            fields = {"a", "b", "near", "far", "valueException", "outException", "out"}
            require_keys(case, fields if fails else fields | {"value"})
            require_keys(case["out"], matrix({}))
            if not fails:
                require_keys(case["value"], matrix({}))


def same_pair(left, right):
    differences = []
    compare.compare("", list(left), list(right), 0, differences)
    compare.compare("", list(right), list(left), 0, differences)
    return not differences


def audit(fna, cna):
    identity = compare.reference_identity(fna)
    candidate, _ = compare.candidate_metadata(cna)
    if candidate is not None and candidate != identity:
        raise ValueError("candidate reference identity differs")
    if identity["sourceRevision"] != REVISION:
        raise ValueError("edge dispositions require the reviewed FNA revision")
    for document, is_reference in ((fna, True), (cna, False)):
        compare.validate_categories(document, CATEGORIES)
        validate_witness_shape(document, is_reference)
    mismatches = []
    for category in CATEGORIES:
        compare.compare("/" + category, fna[category], cna[category], 0, mismatches)
    actual = {path: (left, right) for path, left, right in mismatches}
    expected = retained_differences()
    # Also fail if a retained difference disappears: this is a changed contract to review.
    unexpected = sorted(path for path in actual if path not in expected or not same_pair(actual[path], expected[path]))
    if set(actual) != set(expected) or unexpected:
        missing = sorted(path for path in expected if path not in actual)
        raise ValueError(f"unreviewed differences: {unexpected}; missing retained witnesses: {missing}")
    return len(mismatches)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fna_json")
    parser.add_argument("cna_json")
    args = parser.parse_args()
    try:
        count = audit(compare.load_dump(args.fna_json), compare.load_dump(args.cna_json))
    except (OSError, ValueError, RecursionError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    print(f"PASS: {count} exact retained XNA differences; all other selected fields match at zero tolerance. NOT FNA parity.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
