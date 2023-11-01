
import numpy as np
import random
import os

# Function to generate Latin square
def generate_latin_square(n):
    base = np.arange(n)
    square = np.zeros((n, n), dtype=int)
    for i in range(n):
        square[i] = np.roll(base, -i)
    return square

# Function to create and write to the configuration file
def write_config_file(participant_id, paradigm, task_order, traffic_order):
    lines = []
    # General section
    lines.append("[General]")
    lines.append(f'ParticipantID="{participant_id}"')
    lines.append('LogPerformance="True"')
    lines.append(f'InterruptionParadigm="{paradigm}"')
    lines.append('CurrentBlock="Block1Trial1"')
    lines.append("")  # Empty line for separation
    
    for block_num, task in enumerate(task_order, 1):
        for trial_num, traffic in enumerate(traffic_order[block_num-1], 1):
            block_trial = f"Block{block_num}Trial{trial_num}"
            lines.append(f"[{block_trial}]")
            lines.append('NDRTTaskType="NBackTask"')
            lines.append(f'TaskSetting="{NDRT_TASKS[task]}"')
            lines.append(f'Traffic="{traffic}"')
            lines.append("")  # Empty line for separation
            
    with open(os.path.join(config_dir, f"ExperimentConfig_{participant_id}.ini"), 'w') as configfile:
        configfile.write("\n".join(lines))

# Constants
NDRT_TASKS = ["One", "Two", "Three"]
TRAFFIC_COMPLEXITIES = ["1RV", "3RV", "5RV"]
INTERRUPTION_PARADIGMS = ["SelfRegulated", "SystemRecommended", "SystemInitiated"]
config_dir = "config_files"

# Create directory if it doesn't exist
if not os.path.exists(config_dir):
    os.makedirs(config_dir)

# Generate Latin square
latin_square = generate_latin_square(3)

# Create the config files
participant_counter = 1
for order in [latin_square, latin_square.T]:  # Using both Latin square and its transpose
    for task_order in order:
        for paradigm in INTERRUPTION_PARADIGMS:  # Alternate paradigms for every file
            # Generate randomized traffic order for each NDRT task
            traffic_order = [random.sample(TRAFFIC_COMPLEXITIES, len(TRAFFIC_COMPLEXITIES)) for _ in range(3)]
            participant_id = f"P{str(participant_counter).zfill(2)}"
            write_config_file(participant_id, paradigm, task_order, traffic_order)
            participant_counter += 1