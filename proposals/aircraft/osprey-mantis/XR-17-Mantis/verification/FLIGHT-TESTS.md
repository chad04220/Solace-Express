# Isolated Mantis flight-spec tests

These tests exercise the final AircraftSpec (S=30, vr=50, vref=58) in the baseline conventional dynamics. They do not install or validate the Tier B renderer, camera feeds, weapon gameplay, store mass/aerodynamics, or the planned mesh baker.

Baseline: a2987713ceeb33e3a97560d0c6b85efaf0e52af8. The Mantis spec was temporarily inserted at career index 7 solely to reuse flight tests; this is not the research-registry integration plan. XR-9/XR-11 constants were shifted to 8/9. Production source was not changed.

- Standard flight_test: 0 failures. Initial 7,900 kg takeoff case: 650 m; climb 13.9 m/s. Autoland completed, touchdown 1.0 m/s. See full log for test masses and cases.
- Supplemental initial 7,990 kg Mantis cases (including 90 kg pilot): 0 failures. Fuel burns normally. The derivative is based on flight_test, restricts career loops to Mantis and initializes its fuel/payload at the brief's maximum; unrelated XR-9 checks are retained. See full log.

Reproduce from a pristine baseline checkout using Python 3:

    python verification/prepare_flight_copy.py /path/to/baseline /path/to/new-fixture
    cd /path/to/new-fixture
    g++ -std=c++17 -O2 -pthread tests/flight_test.cpp src/{world,scenery,entities,airport_scenery,aircraft,aircraft_perf,aircraft_stunt,aero,career}.cpp -o flight_test
    ./flight_test
    g++ -std=c++17 -O2 -pthread tests/mantis_max_weight_test.cpp src/{world,scenery,entities,airport_scenery,aircraft,aircraft_perf,aircraft_stunt,aero,career}.cpp -o mantis_max_weight_test
    ./mantis_max_weight_test

Shell brace expansion assumes Bash. The baseline has additional tests; only this flight suite and supplement were run for the Mantis. Osprey's separate package records its own full CTest run.
