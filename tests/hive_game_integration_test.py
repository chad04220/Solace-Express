"""Small source-contract guard for the intentional military/sandbox boundaries.
Dynamic combat and equipment behavior are covered by their CPU tests.
"""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
# (the Game class's own files, as game.cpp was split in v4.4.3: the flow, the hive sorties and the flight each in theirs)
g=''.join((root/'src'/f).read_text() for f in ['game.cpp','game_input.cpp','game_career_flow.cpp','game_regions.cpp','game_flight.cpp',
          'game_weather.cpp','game_loading.cpp','game_cameras.cpp','game_effects.cpp','game_frame.cpp','game_comms.cpp','game_debug_scenes.cpp','game_hive.cpp'])
w=(root/'src/game_somnus.cpp').read_text()
u=(root/'src/game_ui.cpp').read_text()
r=(root/'src/game_research_ui.cpp').read_text()
def body(name,next_name):
    return g.split('void Game::'+name,1)[1].split('void Game::'+next_name,1)[0]
launch=body('launchMilitary','setupMilitaryEncounter')
assert 'beginMilitary(c)' in launch and 'k.accept(' not in launch
assert 'commitLaunch' in launch
assert 'if (militaryFlight)' in body('restartFlight','retryFromDebrief')
assert 'isMilitaryContract(contract.type)' in body('retryFromDebrief','returnToFreeFlight')
assert 'gs < 2.5f && !researchFlight && !militaryFlight' in g
assert g.count('if (k.military.activeAttempt) { k.abandonMilitaryAttempt(); return; }')==2
assert 'researchFlight || isolatedFlight || militaryFlight || crashed' in g
assert 'militaryFlight || hiveCombat.aliveCount()>0' in g
assert 'if (!researchFlight || resCard>=0 || specIdx!=kSomnus' in g
assert 'hiveCombat.aliveCount()<hive::MaxActors' in g
assert 'hiveCombat.playerShot' in w and 'hiveCombat.playerEMP' in w and 'hiveCombat.playerBlast' in w
assert 'combatLoadout.releaseBomb()' in w and 'combatLoadout.step(' in w
assert 'militarySettled=true' in g and 'mission.status==hive::MissionStatus::Success' in g
assert 'drawCombatPractice' in r and 'Clear / rearm' in r
assert 'STRIKE TARGET' in u and 'SCAN SITE' in u and 'EXTRACTION' in u
print('hive_game_integration_test: military/practice lifecycle and equipment contracts passed')
