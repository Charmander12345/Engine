import pathlib
root = pathlib.Path(__file__).parent

# --- Insert Physics section before Landscape ---
p = root / 'PROJECT_OVERVIEW.md'
t = p.read_text(encoding='utf-8')
lines = t.split('\n')
idx = [i for i,l in enumerate(lines) if '## 16. Landscape-System' in l]
print('landscape idx:', idx)
if idx:
    blk = [
        '## 15. Physik-System',
        '',
        '**Dateien:** src/Physics/PhysicsWorld.h, src/Physics/PhysicsWorld.cpp',
        '',
        '### 15.1 \u00dcbersicht',
        'Eigene Rigid-Body-Physik-Simulation als Singleton (PhysicsWorld::Instance()). L\u00e4uft nur w\u00e4hrend PIE (Play In Editor). Verwendet Fixed-Timestep-Akkumulator (Standard: 1/60s, max. 0.1s Clamp).',
        '',
        '### 15.2 Architektur',
        '',
        '- step(dt): Akkumulator-Pattern, clamp auf 0.1s',
        '- gatherBodies(): ECS Transform+Physics \u2192 interner RigidBody-Vektor',
        '- integrate(): Semi-impliziter Euler mit Gravitation',
        '- detectCollisions(): N\u00b2-Broadphase mit 3 Testfunktionen',
        '- 
esolveCollisions(): Impulsbasiert + Positionskorrektur + Reibung',
        '- writeback(): Positionen und Geschwindigkeiten zur\u00fcck ins ECS',
        '',
        '### 15.3 Kollisionserkennung',
        '',
        '| Test                  | Beschreibung                                      |',
        '|-----------------------|---------------------------------------------------|',
        '| 	estSphereSphere()  | Distanzbasiert mit skaliertem Radius              |',
        '| 	estBoxBox()        | AABB Separating-Axis, minimale Durchdringungsachse|',
        '| 	estSphereBox()     | N\u00e4chster Punkt auf AABB                           |',
        '',
        '### 15.4 Kollisionsaufl\u00f6sung',
        '- **Impulsbasiert**: Restitution aus Minimum beider K\u00f6rper',
        '- **Positionskorrektur**: Baumgarte-Stabilisierung (Slop=0.005, Korrektur=0.4)',
        '- **Reibung**: Coulomb-Modell (Tangential-Impuls begrenzt auf \u03bc\u00b7|Jn|)',
        '- Statische/kinematische K\u00f6rper werden bei der Aufl\u00f6sung nicht bewegt',
        '',
        '### 15.5 Integration in main.cpp',
        '- PIE Start: PhysicsWorld::Instance().initialize() (nach snapshotEcsState())',
        '- PIE Loop: PhysicsWorld::Instance().step(dt) (nach Scripting::UpdateScripts(dt))',
        '- PIE Stop: PhysicsWorld::Instance().shutdown() (vor 
estoreEcsSnapshot())',
        '',
        '### 15.6 Python-API (engine.physics)',
        'Siehe engine.physics in Abschnitt 11.2.',
        '',
        '---',
        '',
    ]
    for j, b in enumerate(blk):
        lines.insert(idx[0] + j, b)
    p.write_text('\n'.join(lines), encoding='utf-8')
    print('Physics section inserted')
else:
    print('ERROR: Landscape heading not found')
