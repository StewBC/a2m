import os
import sys
import re

def collect_symbols(file_paths):
    """First pass: Collect global and scoped symbols from all files."""
    global_symbols = set()
    function_scopes = []
    scoped_labels = {}
    scoped_variables = {}
    proc_counter = 0
    current_function = None

    # Patterns
    label_pattern = re.compile(r'^(\s*)(\w+):')
    variable_pattern = re.compile(r'^(\s*)(\w+)\s*=')
    proc_pattern = re.compile(r'\.proc\s+(\w+)')

    for file_path in file_paths:
        with open(file_path, 'r') as f:
            for line in f:
                # Detect the start of a .proc block
                proc_match = proc_pattern.match(line)
                if proc_match:
                    current_function = proc_match.group(1)
                    proc_counter += 1
                    global_symbols.add(current_function)
                    function_scopes.append((current_function, proc_counter))
                    scoped_labels[current_function] = set()
                    scoped_variables[current_function] = set()
                    continue

                # Detect the end of a .proc block
                if re.match(r'\.endproc', line):
                    current_function = None
                    continue

                # Collect global variables and labels
                if current_function is None:
                    variable_match = variable_pattern.match(line)
                    if variable_match:
                        _, variable = variable_match.groups()
                        global_symbols.add(variable)

                # Collect scoped labels and variables
                if current_function:
                    label_match = label_pattern.match(line)
                    if label_match:
                        _, label = label_match.groups()
                        scoped_labels[current_function].add(label)

                    variable_match = variable_pattern.match(line)
                    if variable_match:
                        _, variable = variable_match.groups()
                        scoped_variables[current_function].add(variable)

    return global_symbols, function_scopes, scoped_labels, scoped_variables


def transform_file(file_path, global_symbols, function_scopes, scoped_labels, scoped_variables):
    """Second pass: Transform labels and variables in a single file."""
    output_lines = []
    proc_pattern = re.compile(r'\.proc\s+(\w+)')
    current_function = None

    with open(file_path, 'r') as f:
        for line in f:
            original_line = line  # Preserve original spacing and line structure

            # Detect the start of a .proc block
            proc_match = proc_pattern.match(line)
            if proc_match:
                current_function = proc_match.group(1)
                proc_number = next(num for name, num in function_scopes if name == current_function)
                output_lines.append(original_line)  # Use the unmodified original line
                continue

            # Detect the end of a .proc block
            if re.match(r'\.endproc', line):
                current_function = None
                output_lines.append(original_line)  # Use the unmodified original line
                continue

            if current_function:
                proc_number = next(num for name, num in function_scopes if name == current_function)

                # Descope labels
                for label in scoped_labels[current_function]:
                    clash = (
                        label in global_symbols or
                        any(label in scoped_labels[func] and func != current_function for func in scoped_labels)
                    )
                    if clash:
                        line = re.sub(rf'\b{label}\b', f'{label}_{proc_number}', line)

                # Descope variables
                for variable in scoped_variables[current_function]:
                    clash = (
                        variable in global_symbols or
                        any(variable in scoped_variables[func] and func != current_function for func in scoped_variables)
                    )
                    if clash:
                        line = re.sub(rf'\b{variable}\b', f'{variable}_{proc_number}', line)

            # Append the processed line
            output_lines.append(line if line != original_line else original_line)

    return ''.join(output_lines)


def process_directory(dir_path):
    # Get all files in the directory with .asm, .s or .inc extensions
    file_paths = [
        os.path.join(dir_path, f)
        for f in os.listdir(dir_path)
        if os.path.isfile(os.path.join(dir_path, f)) and (f.endswith('.asm') or f.endswith('.s') or f.endswith('.inc'))
    ]

    # Pass 1: Collect all global and scoped symbols
    global_symbols, function_scopes, scoped_labels, scoped_variables = collect_symbols(file_paths)

    # Pass 2: Transform files based on collected symbols
    for file_path in file_paths:
        transformed_content = transform_file(file_path, global_symbols, function_scopes, scoped_labels, scoped_variables)
        with open(file_path, 'w') as f:
            f.write(transformed_content)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: descope.py <directory_path>")
        sys.exit(1)

    directory_path = sys.argv[1]
    if not os.path.isdir(directory_path):
        print(f"Error: {directory_path} is not a valid directory.")
        sys.exit(1)

    process_directory(directory_path)
