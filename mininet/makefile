test: MPDtest run_tests.py
	sudo python run_tests.py

v1.3: MPDtest run_tests.py ./bin/MPDtest_v1.3
	sudo python run_tests.py ./bin/MPDtest_v1.3

v1.2: MPDtest run_tests.py ./bin/MPDtest_v1.2
	sudo python run_tests.py ./bin/MPDtest_v1.2

v1.4: MPDtest run_tests.py ./bin/MPDtest_v1.4
	sudo python run_tests.py ./bin/MPDtest_v1.4

reno: MPDtest run_tests.py ./bin/MPDtest_reno
	sudo python run_tests.py ./bin/MPDtest_reno

demo: MPDtest twohosts_twoswitches.py
	sudo python twohosts_twoswitches.py

clean:
	sudo mn -c
	sudo sudo pgrep -a servertest | sudo pkill -f -
	sudo sudo pgrep -a servertest 
