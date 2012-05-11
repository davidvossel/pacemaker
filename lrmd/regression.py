#!/usr/bin/python

#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA.
import os
import sys
import subprocess
import shlex
import time

class Test:
	def __init__(self, name, description, lrmd_location, test_tool_location):
		self.name = name
		self.description = description
		self.cmds = []
		self.iteration = 0;
		self.daemon_location = lrmd_location
		self.test_tool_location = test_tool_location

		self.result_txt = ""
		self.result_exitcode = 0;

		self.executed = 0

	def __new_cmd(self, cmd, args, exitcode, stdout_match):
		self.cmds.append(
			{
				"cmd" : cmd,
				"args" : args,
				"expected_exitcode" : exitcode,
				"stdout_match" : stdout_match
			}
		)

	def add_sys_cmd(self, cmd, args):
		self.__new_cmd(cmd, args, 0, "")

	def add_cmd_check_stdout(self, args, stdout_match):
		self.__new_cmd(self.test_tool_location, args, 0, stdout_match)

	def add_cmd(self, args):
		self.__new_cmd(self.test_tool_location, args, 0, "")

	def add_expected_fail_cmd(self, args):
		self.__new_cmd(self.test_tool_location, args, 255, "")

	def get_exitcode(self):
		return self.result_exitcode

	def print_result(self):
		print "    %s" % self.result_txt

	def run_cmd(self, args):

		cmd = shlex.split(args['args'])
		cmd.insert(0, args['cmd'])
		test = subprocess.Popen(cmd, stdout=subprocess.PIPE)
		test.wait()

		if args['stdout_match'] != "" and test.communicate()[0].count(args['stdout_match']) == 0:
			test.returncode = -2
			print "STDOUT string '%s' was not found in cmd output" % (args['stdout_match'])

		return test.returncode;

	def run(self):
		res = 0
		i = 1
		print "\n--- BEGIN LRMD TEST %s " % self.name
		self.result_txt = "SUCCESS - '%s'" % (self.name)
		self.result_exitcode = 0
		lrmd = subprocess.Popen("./lrmd")
		for cmd in self.cmds:
			res = self.run_cmd(cmd)
			if res != cmd['expected_exitcode']:
				print "Iteration %d FAILED - pid rc %d expected rc %d - cmd args '%s'" % (i, res, cmd['expected_exitcode'], cmd['args'])
				self.result_txt = "FAILURE - '%s' failed on cmd iteration %d" % (self.name, i)
				self.result_exitcode = -1
				break
			else:
				print "Iteration %d SUCCESS" % (i)
			i = i + 1
		print "--- END LRMD '%s' TEST - %s \n" % (self.name, self.result_txt)
		lrmd.kill()

		self.executed = 1
		return res

class Tests:
	def __init__(self, lrmd_location, test_tool_location):
		self.daemon_location = lrmd_location
		self.test_tool_location = test_tool_location
		self.tests = []

	def new_test(self, name, description):
		test = Test(name, description, self.daemon_location, self.test_tool_location)
		self.tests.append(test)
		return test

	def build_tests(self):
		### register/unregister test ###
		test = self.new_test("registration", "Simple resource registration test");
		test.add_cmd("-c register_rsc "
			"-r \"test_rsc\" "
			"-C \"ocf\" "
			"-P \"pacemaker\" "
			"-T \"Dummy\" "
			"-l \"NEW_EVENT event_type:0 rsc_id:test_rsc action:none rc:0 exec_rc:OCF_OK op_status:OP_DONE\" "
			"-t 1000")
		test.add_cmd("-c unregister_rsc -r \"test_rsc\" -t 1000 "
			"-l \"NEW_EVENT event_type:1 rsc_id:test_rsc action:none rc:0 exec_rc:OCF_OK op_status:OP_DONE\" ")

		### start/stop test ###
		test = self.new_test("start_stop", "Register a test, then start and stop it");
		test.add_cmd("-c register_rsc "
			"-r \"test_rsc\" "
			"-C \"ocf\" "
			"-P \"pacemaker\" "
			"-T \"Dummy\" "
			"-l \"NEW_EVENT event_type:0 rsc_id:test_rsc action:none rc:0 exec_rc:OCF_OK op_status:OP_DONE\" "
			"-t 1000")
		test.add_cmd("-c exec "
			"-r \"test_rsc\" "
			"-a \"start\" "
			"-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:start rc:0 exec_rc:OCF_OK op_status:OP_DONE\" "
			"-t 1000")
		test.add_cmd("-c exec "
			"-r \"test_rsc\" "
			"-a \"stop\" "
			"-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:stop rc:0 exec_rc:OCF_OK op_status:OP_DONE\" "
			"-t 1000")
		test.add_cmd("-c unregister_rsc -r \"test_rsc\" -t 1000 "
			"-l \"NEW_EVENT event_type:1 rsc_id:test_rsc action:none rc:0 exec_rc:OCF_OK op_status:OP_DONE\" ")

		### start delay /stop test ###
		test = self.new_test("start_delay_stop", "Register a test, then start with start_delay value, and stop it");
		test.add_cmd("-c register_rsc -r test_rsc -P pacemaker -C ocf -T Dummy "
			"-l \"NEW_EVENT event_type:0 rsc_id:test_rsc action:none rc:0 exec_rc:OCF_OK op_status:OP_DONE\" -t 1000")
		test.add_cmd("-c exec -r test_rsc -s 2000 -a start -w -t 1000")
		test.add_expected_fail_cmd("-l "
			"\"NEW_EVENT event_type:2 rsc_id:test_rsc action:start rc:0 exec_rc:OCF_OK op_status:OP_DONE\" -t 1000")
		test.add_cmd("-l "
			"\"NEW_EVENT event_type:2 rsc_id:test_rsc action:start rc:0 exec_rc:OCF_OK op_status:OP_DONE\" -t 3000")
		test.add_cmd("-c exec -r test_rsc -a stop -t 1000"
			"-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:stop rc:0 exec_rc:OCF_OK op_status:OP_DONE\" ")
		test.add_cmd("-c unregister_rsc -r test_rsc -t 1000 "
			"-l \"NEW_EVENT event_type:1 rsc_id:test_rsc action:none rc:0 exec_rc:OCF_OK op_status:OP_DONE\" ")

		### monitor test ###
		test = self.new_test("monitor_test", "Register a test, the start, monitor a few times, then stop");
		test.add_cmd("-c register_rsc "
			"-r \"test_rsc\" "
			"-C \"ocf\" "
			"-P \"pacemaker\" "
			"-T \"Dummy\" "
			"-l \"NEW_EVENT event_type:0 rsc_id:test_rsc action:none rc:0 exec_rc:OCF_OK op_status:OP_DONE\" "
			"-t 1000")
		test.add_cmd("-c exec "
			"-r \"test_rsc\" "
			"-a \"start\" "
			"-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:start rc:0 exec_rc:OCF_OK op_status:OP_DONE\" "
			"-t 1000")
		test.add_cmd("-c exec "
			"-r \"test_rsc\" "
			"-a \"monitor\" "
			"-i \"1000\" "
			"-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:monitor rc:0 exec_rc:OCF_OK op_status:OP_DONE\" "
			"-t 1000")
		test.add_cmd("-c exec "
			"-r \"test_rsc\" "
			"-a \"stop\" "
			"-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:stop rc:0 exec_rc:OCF_OK op_status:OP_DONE\" "
			"-t 1000")
		test.add_cmd("-c unregister_rsc -r \"test_rsc\" -t 1000 "
			"-l \"NEW_EVENT event_type:1 rsc_id:test_rsc action:none rc:0 exec_rc:OCF_OK op_status:OP_DONE\" ")

		### monitor and cancel test ###
		test = self.new_test("monitor_and_cancel_test", "Register a test, the start, monitor a few times, then cancel the monitor");
		test.add_cmd("-c register_rsc "
			"-r \"test_rsc\" "
			"-C \"ocf\" "
			"-P \"pacemaker\" "
			"-T \"Dummy\" "
			"-l \"NEW_EVENT event_type:0 rsc_id:test_rsc action:none rc:0 exec_rc:OCF_OK op_status:OP_DONE\" "
			"-t 1000")
		test.add_cmd("-c exec "
			"-r \"test_rsc\" "
			"-a \"start\" "
			"-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:start rc:0 exec_rc:OCF_OK op_status:OP_DONE\" "
			"-t 1000")
		test.add_cmd("-c exec "
			"-r \"test_rsc\" "
			"-a \"monitor\" "
			"-i \"100\" "
			"-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:monitor rc:0 exec_rc:OCF_OK op_status:OP_DONE\" "
			"-t 1000")
		test.add_cmd("-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:monitor rc:0 exec_rc:OCF_OK op_status:OP_DONE\" -t 2000")
		test.add_cmd("-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:monitor rc:0 exec_rc:OCF_OK op_status:OP_DONE\" -t 2000")
		test.add_cmd("-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:monitor rc:0 exec_rc:OCF_OK op_status:OP_DONE\" -t 2000")
		test.add_cmd("-c cancel "
			"-r \"test_rsc\" "
			"-a \"monitor\" "
			"-i \"100\" "
			"-t \"1000\" "
			"-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:monitor rc:0 exec_rc:OCF_OK op_status:OP_CANCELLED\" ")
		test.add_expected_fail_cmd("-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:monitor rc:0 exec_rc:OCF_OK op_status:OP_DONE\" -t 1000")
		test.add_cmd("-c exec "
			"-r \"test_rsc\" "
			"-a \"stop\" "
			"-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:stop rc:0 exec_rc:OCF_OK op_status:OP_DONE\" "
			"-t 1000")
		test.add_cmd("-c unregister_rsc -r \"test_rsc\" -t 1000 "
			"-l \"NEW_EVENT event_type:1 rsc_id:test_rsc action:none rc:0 exec_rc:OCF_OK op_status:OP_DONE\" ")

		### monitor fail ###
		test = self.new_test("monitor_fail_test", "Register a test, the start, monitor a few times, then make the monitor fail.");
		test.add_cmd("-c register_rsc "
			"-r \"test_rsc\" "
			"-C \"ocf\" "
			"-P \"pacemaker\" "
			"-T \"Dummy\" "
			"-l \"NEW_EVENT event_type:0 rsc_id:test_rsc action:none rc:0 exec_rc:OCF_OK op_status:OP_DONE\" "
			"-t 1000")
		test.add_cmd("-c exec "
			"-r \"test_rsc\" "
			"-a \"start\" "
			"-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:start rc:0 exec_rc:OCF_OK op_status:OP_DONE\" "
			"-t 1000")
		test.add_cmd("-c exec "
			"-r \"test_rsc\" "
			"-a \"monitor\" "
			"-i \"100\" "
			"-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:monitor rc:0 exec_rc:OCF_OK op_status:OP_DONE\" "
			"-t 1000")
		test.add_cmd("-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:monitor rc:0 exec_rc:OCF_OK op_status:OP_DONE\" -t 2000")
		test.add_cmd("-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:monitor rc:0 exec_rc:OCF_OK op_status:OP_DONE\" -t 2000")
		test.add_sys_cmd("rm", "-f /var/run/Dummy-test_rsc.state")
		test.add_cmd("-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:monitor rc:0 exec_rc:OCF_NOT_RUNNING op_status:OP_DONE\" -t 2000")
		test.add_cmd("-c cancel "
			"-r \"test_rsc\" "
			"-a \"monitor\" "
			"-i \"100\" "
			"-t \"1000\" "
			"-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:monitor rc:0 exec_rc:OCF_NOT_RUNNING op_status:OP_CANCELLED\" ")
		test.add_expected_fail_cmd("-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:monitor rc:0 exec_rc:OCF_NOT_RUNNING op_status:OP_DONE\" -t 1000")
		test.add_expected_fail_cmd("-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:monitor rc:0 exec_rc:OCF_OK op_status:OP_DONE\" -t 1000")
		test.add_cmd("-c unregister_rsc -r \"test_rsc\" -t 1000 "
			"-l \"NEW_EVENT event_type:1 rsc_id:test_rsc action:none rc:0 exec_rc:OCF_OK op_status:OP_DONE\" ")

		### get metadata ###
		test = self.new_test("get_metadata", "Retrieve metadata for a resource");
		test.add_cmd_check_stdout("-c metadata "
			"-C \"ocf\" "
			"-P \"pacemaker\" "
			"-T \"Dummy\"" ,"resource-agent name=\"Dummy\"")
		test.add_cmd("-c metadata "
			"-C \"ocf\" "
			"-P \"pacemaker\" "
			"-T \"Stateful\"")
		test.add_expected_fail_cmd("-c metadata "
			"-P \"pacemaker\" "
			"-T \"Stateful\"")
		test.add_expected_fail_cmd("-c metadata "
			"-C \"ocf\" "
			"-P \"pacemaker\" "
			"-T \"fake_agent\"")

		### get stonith metadata ###
		test = self.new_test("get_stonith_metadata", "Retrieve stonith metadata for a resource");
		test.add_cmd_check_stdout("-c metadata "
			"-C \"stonith\" "
			"-P \"pacemaker\" "
			"-T \"fence_pcmk\"", "resource-agent name=\"fence_pcmk\"")

		### get agents ###
		test = self.new_test("list_agents", "Retrieve list of available resource agents, verifies at least one agent exists.");
		test.add_cmd_check_stdout("-c list_agents ", "Dummy");

		### get stonith agents  ###
		test = self.new_test("check_stonith_agents", "Retrieve list of available resource agents, verifies fence_pcmk exists");
		test.add_cmd_check_stdout("-c list_agents ", "fence_pcmk");

		### get providers  ###
		test = self.new_test("list_providers", "Retrieve list of available resource providers, verifies pacemaker is a provider.");
		test.add_cmd_check_stdout("-c list_providers ", "pacemaker");
		test.add_cmd_check_stdout("-c list_providers -T ping", "pacemaker");

	def print_list(self):
		print "\n==== %d TESTS FOUND ====" % (len(self.tests))
		for test in self.tests:
			print "%s             - %s" % (test.name, test.description)
		print "==== END OF LIST ====\n"

	def run_single(self, name):
		for test in self.tests:
			if test.name == name:
				test.run()
				break;

	def run_tests(self):
		for test in self.tests:
			test.run()

	def print_results(self):
		failures = 0;
		success = 0;
		print "\n\n======= FINAL RESULTS =========="
		print "\n--- INDIVIDUAL TEST RESULTS:"
		for test in self.tests:
			if test.executed == 0:
				continue

			test.print_result()
			if test.get_exitcode() != 0:
				failures = failures + 1
			else:
				success = success + 1

		print "\n--- TOTALS\n    Pass:%d\n    Fail:%d\n" % (success, failures)

class TestOptions:
	def __init__(self):
		self.options = {}
		self.options['list-tests'] = 0
		self.options['run-all'] = 1
		self.options['run-only'] = ""
		self.options['invalid-arg'] = ""
		self.options['show-usage'] = 0

	def build_options(self, argv):
		args = argv[1:]
		skip = 0
		for i in range(0, len(args)):
			if skip:
				skip = 0
				continue
			elif args[i] == "-h" or args[i] == "--help":
				self.options['show-usage'] = 1
			elif args[i] == "-l" or args[i] == "--list-tests":
				self.options['list-tests'] = 1
			elif args[i] == "-r" or args[i] == "--run-only":
				self.options['run-only'] = args[i+1]
				skip = 1

	def show_usage(self):
		print "usage: " + sys.argv[0] + " [options]"
		print "If no options are provided, all tests will run"
		print "Options:"
		print "\t [--help | -h]                     Show usage"
		print "\t [--list-tests | -l]               Print out all registered tests."
		print "\t [--run-only | -r 'testname']      Run a specific test"
		print "\n\tExample: "
		print "\t\t python ./regression.py --run-only start_stop"


def main(argv):
	o = TestOptions()
	o.build_options(argv)

	tests = Tests("./lrmd", "./lrmd_test");
	tests.build_tests()

	if o.options['list-tests']:
		tests.print_list()
	elif o.options['show-usage']:
		o.show_usage()
	elif o.options['run-only'] != "":
		tests.run_single(o.options['run-only'])
		tests.print_results()
	else:
		tests.run_tests()
		tests.print_results()

if __name__=="__main__":
	main(sys.argv)
