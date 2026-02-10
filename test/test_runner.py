import subprocess
import os
import shlex
import time
import difflib
import sys
import re
import platform

def is_windows():
    """Check if running on Windows (not WSL)"""
    return platform.system() == "Windows"

def is_wsl():
    """Check if running in WSL"""
    try:
        with open('/proc/version', 'r') as f:
            return 'microsoft' in f.read().lower()
    except:
        return False

def is_valgrind_available():
    """Check if valgrind is available on the system"""
    if is_windows() and not is_wsl():
        # On native Windows, valgrind is not available
        return False

    try:
        result = subprocess.run(
            ["valgrind", "--version"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=5
        )
        return result.returncode == 0
    except (subprocess.CalledProcessError, FileNotFoundError, subprocess.TimeoutExpired):
        return False

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

def parse_valgrind_output(valgrind_log):
    """Parse valgrind output to detect memory issues"""
    issues = []

    try:
        with open(valgrind_log, 'r') as f:
            content = f.read()

        # Check for memory leaks
        leak_match = re.search(r'definitely lost: ([\d,]+) bytes', content)
        if leak_match:
            leaked = leak_match.group(1).replace(',', '')
            if int(leaked) > 0:
                issues.append(f"Memory leak: {leak_match.group(1)} bytes definitely lost")

        # Check for invalid reads/writes
        invalid_read = re.search(r'Invalid read of size \d+', content)
        if invalid_read:
            issues.append(f"Invalid memory read detected")

        invalid_write = re.search(r'Invalid write of size \d+', content)
        if invalid_write:
            issues.append(f"Invalid memory write detected")

        # Check for use of uninitialized values
        uninit = re.search(r'Conditional jump or move depends on uninitialised value', content)
        if uninit:
            issues.append(f"Use of uninitialized value detected")

        # Check for general error summary
        error_match = re.search(r'ERROR SUMMARY: (\d+) errors', content)
        if error_match and int(error_match.group(1)) > 0:
            if not issues:  # Only add if we haven't already detected specific issues
                issues.append(f"{error_match.group(1)} error(s) detected by valgrind")

    except FileNotFoundError:
        issues.append("Valgrind log file not found")
    except Exception as e:
        issues.append(f"Error parsing valgrind output: {e}")

    return issues

def format_time(seconds):
    """Format elapsed time in a human-readable way"""
    if seconds < 1:
        return f"{seconds*1000:.0f}ms"
    elif seconds < 60:
        return f"{seconds:.3f}s"
    else:
        minutes = int(seconds // 60)
        secs = seconds % 60
        return f"{minutes}m {secs:.3f}s"

def run_test_file(test_path, exec_path="../Filesystem", use_valgrind=False):
    start_time = time.time()
    test_passed = True
    valgrind_issues = []

    # Adjust executable path for Windows
    if is_windows() and not exec_path.endswith('.exe'):
        exec_path += '.exe'

    with open(test_path, 'r') as f:
        lines = [line.rstrip() for line in f if line.strip() and not line.strip().startswith("#")]

    # Build command
    if use_valgrind:
        if is_windows() and not is_wsl():
            print("\n\033[91mError: Valgrind is not available on native Windows\033[0m")
            print("Please use WSL (Windows Subsystem for Linux) to run valgrind tests")
            return False, 0

        valgrind_log = f"valgrind_{os.path.basename(test_path)}.log"
        cmd = [
            "valgrind",
            "--leak-check=full",
            "--show-leak-kinds=all",
            "--track-origins=yes",
            "--log-file=" + valgrind_log,
            exec_path,
            "verbose"
        ]
    else:
        cmd = [exec_path, "verbose"]

    # Start the executable (persistent process)
    proc = subprocess.Popen(
        cmd,
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
                    while i < len(lines) and not lines[i].startswith("SEND ") and not lines[i].startswith("FILE_VERIFY"):
                        expected_lines.append(lines[i])
                        i += 1
                else:
                    expected_lines.append(lines[i][7:])
                    i += 1
                expected_output = "\n".join(expected_lines).strip()

                # Read actual output
                actual_output = read_until_prompt(proc)

                # Compare normalized versions
                expected_normalized = normalize_output(expected_output)
                actual_normalized = normalize_output(actual_output)

                if expected_normalized != actual_normalized:
                    print(f"\nOutput mismatch for command: {cmd.strip()}")
                    print("Expected vs Actual:")
                    # Show the normalized versions in the diff
                    print_diff("\n".join(expected_normalized), "\n".join(actual_normalized))
                    test_passed = False
            else:
                print("Missing EXPECT after SEND")

        elif lines[i].startswith("FILE_VERIFY "):
            # FILE_VERIFY actual_file expected_file
            parts = lines[i][12:].strip().split()
            if len(parts) != 2:
                print(f"\nInvalid FILE_VERIFY syntax: {lines[i]}")
                print("   Expected: FILE_VERIFY <actual_file> <expected_file>")
                test_passed = False
                i += 1
                continue

            actual_file = parts[0]
            expected_file = parts[1]
            i += 1

            # Read both files and compare
            try:
                with open(actual_file, 'r') as f:
                    actual_content = f.read()

                with open(expected_file, 'r') as f:
                    expected_content = f.read()

                # Compare
                if expected_content != actual_content:
                    print(f"\nFile content mismatch for: {actual_file}")
                    print(f"Expected content from: {expected_file}")
                    print("Expected vs Actual:")
                    print_diff(expected_content, actual_content)
                    test_passed = False

                # Clean up - delete the actual file after verification
                os.remove(actual_file)
            except FileNotFoundError as e:
                print(f"\nFile not found: {e.filename}")
                test_passed = False
            except Exception as e:
                print(f"\nError reading files: {e}")
                test_passed = False

        else:
            i += 1

    proc.stdin.close()
    proc.terminate()
    proc.wait()  # Wait for process to fully terminate

    # Check valgrind results if enabled
    if use_valgrind:
        valgrind_log = f"valgrind_{os.path.basename(test_path)}.log"
        valgrind_issues = parse_valgrind_output(valgrind_log)

        if valgrind_issues:
            test_passed = False
            print("\n\033[91mValgrind detected memory issues:\033[0m")
            for issue in valgrind_issues:
                print(f"  - {issue}")
            print(f"\nSee {valgrind_log} for full details")
        else:
            # Clean up valgrind log if no issues
            try:
                os.remove(valgrind_log)
            except:
                pass

    elapsed_time = time.time() - start_time

    if test_passed:
        print(f"\033[92mPASSED\033[0m ({format_time(elapsed_time)})")
    else:
        print(f"\033[91mFAILED\033[0m ({format_time(elapsed_time)})")

    return test_passed, elapsed_time

# Run tests
if __name__ == "__main__":
    overall_start_time = time.time()
    use_valgrind = False
    test_nums = []

    # Parse command line arguments
    i = 1
    while i < len(sys.argv):
        if sys.argv[i] == "--valgrind":
            use_valgrind = True
        else:
            test_nums.append(sys.argv[i])
        i += 1

    # Check platform and valgrind availability
    if use_valgrind:
        if is_windows() and not is_wsl():
            print("\033[91m" + "="*60 + "\033[0m")
            print("\033[91mValgrind is not available on native Windows\033[0m")
            print("\nTo use Valgrind on Windows, you have two options:")
            print("  1. Use WSL (Windows Subsystem for Linux):")
            print("     - Run 'wsl' in PowerShell/CMD to enter WSL")
            print("     - Install valgrind: sudo apt install valgrind")
            print("     - Run this script from within WSL")
            print("\n  2. Use Dr. Memory (Windows alternative to Valgrind):")
            print("     - Download from: https://drmemory.org/")
            print("     - Run: drmemory.exe -- your_program.exe")
            print("\033[91m" + "="*60 + "\033[0m")
            sys.exit(1)

        if not is_valgrind_available():
            print("\033[91mError: Valgrind is not installed or not in PATH\033[0m")
            if is_wsl():
                print("You are running in WSL. Install valgrind with:")
                print("  sudo apt update")
                print("  sudo apt install valgrind")
            else:
                print("Install valgrind with:")
                print("  sudo apt install valgrind (Ubuntu/Debian)")
                print("  sudo yum install valgrind (RHEL/CentOS)")
                print("  sudo pacman -S valgrind (Arch)")
            sys.exit(1)

        print("Running tests with Valgrind memory analysis...")
        if is_wsl():
            print("(Detected WSL environment)")
        print()
    else:
        # Show platform info for regular runs
        if is_windows():
            print("Running on Windows (native)")
        elif is_wsl():
            print("Running on WSL")
        else:
            print(f"Running on {platform.system()}")
        print()

    all_passed = True
    total_tests = 0
    passed_tests = 0

    if test_nums:
        # Run specific tests provided as command line arguments
        for test_num in test_nums:
            test_file = f"tests/test{test_num}.txt"
            print(f"Running test{test_num}...", end = " ")
            passed, elapsed = run_test_file(test_file, use_valgrind=use_valgrind)
            total_tests += 1
            if passed:
                passed_tests += 1
            all_passed = all_passed and passed
    else:
        # Run all tests
        try:
            num_tests = len(os.listdir("tests"))
            for test in range(num_tests):
                test_file = f"tests/test{test}.txt"
                print(f"Running test{test}...", end = " ")
                passed, elapsed = run_test_file(test_file, use_valgrind=use_valgrind)
                total_tests += 1
                if passed:
                    passed_tests += 1
                all_passed = all_passed and passed
        except FileNotFoundError:
            print("\033[91mError: 'tests' directory not found\033[0m")
            sys.exit(1)

    overall_elapsed_time = time.time() - overall_start_time

    print("\n" + "="*50)
    if all_passed:
        print(f"\033[92mAll tests passed!\033[0m ({passed_tests}/{total_tests})")
    else:
        print(f"\033[91mSome tests failed\033[0m ({passed_tests}/{total_tests} passed)")

    print(f"Total time: {format_time(overall_elapsed_time)}")
    print("="*50)

    sys.exit(0 if all_passed else 1)