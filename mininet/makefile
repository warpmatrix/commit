test: MPDtest topo.py
	sudo python topo.py

demo: MPDtest twohosts_twoswitches.py
	sudo python twohosts_twoswitches.py

variable-bw: MPDtest variable-bw.py
	sudo python variable-bw.py

clean:
	sudo mn -c
	sudo sudo pgrep -a servertest | sudo pkill -f -
	sudo sudo pgrep -a servertest 
