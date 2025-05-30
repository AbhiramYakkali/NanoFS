import subprocess
import os
import shlex
import time
import difflib

def print_diff(expected, actual):
    diff = difflib.ndiff(expected.splitlines(), actual.splitlines())
    for line in diff:
        if line.startswith("- "):
            print(f"\033[91m{line}\033[0m")  # red for missing from actual
        elif line.startswith("+ "):
            print(f"\033[92m{line}\033[0m")  # green for added in actual
        elif line.startswith("? "):
            print(f"\033[93m{line}\033[0m")  # yellow for hint (e.g., ^ markers)
        else:
            print(line)

# Remove trailing and leading whitespace from output
def normalize_output(output):
    return [line.rstrip() for line in output.strip().splitlines()]

# Reads all output from the process until encountering the stop token
def read_until_prompt(proc, prompt='nanofs/>'):
    output = ''
    while True:
        char = proc.stdout.read(1)
        if char == '':
            break
        output += char
        if output.strip().endswith(prompt):
            break

    # Remove the 'nanofs/>' from the end of the output so it doesn't need to be included in the EXPECT sections
    lines = output.strip().splitlines()
    lines.pop()
    return "\n".join(lines).strip()

def run_test_file(test_path, exec_path="../Filesystem"):
    test_passed = True

    with open(test_path, 'r') as f:
        lines = [line.rstrip() for line in f if line.strip() and not line.strip().startswith("#")]

    # Start the executable (persistent process)
    proc = subprocess.Popen(
        [exec_path, "verbose"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=0
    )

    # Read initial prompt
    read_until_prompt(proc)

    i = 0
    while i < len(lines):
        if lines[i].startswith("SEND "):
            cmd = lines[i][5:] + '\n'
            proc.stdin.write(cmd)
            proc.stdin.flush()

            i += 1
            # Collect expected output
            expected_lines = []
            if i < len(lines) and lines[i].startswith("EXPECT"):
                if lines[i] == "EXPECT":
                    i += 1
                    while i < len(lines) and not lines[i].startswith("SEND "):
                        expected_lines.append(lines[i])
                        i += 1
                else:
                    expected_lines.append(lines[i][7:])
                    i += 1
                expected_output = "\n".join(expected_lines).strip()

                # Read actual output
                actual_output = read_until_prompt(proc)

                # Compare
                if normalize_output(expected_output) != normalize_output(actual_output):
                    print(f"\nOutput mismatch for command: {cmd.strip()}")
                    print("Expected vs Actual:")
                    print_diff(expected_output.strip(), actual_output.strip())
                    test_passed = False
            else:
                print("⚠️ Missing EXPECT after SEND")

        else:
            i += 1

    proc.stdin.close()
    proc.terminate()

    if test_passed:
        print("PASSED")
    else:
        print("FAILED")

# Run tests
num_tests = len(os.listdir("tests"))
for test in range(num_tests):
    test_file = f"tests/test{test}.txt"
    print(f"Running test{test}...", end = " ")
    run_test_file(test_file)
