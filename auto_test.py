#!/usr/bin/python3

import os

case = 'case_2'
# case 1 => no active memory pressure with no other options
# case 2 => active memory pressure with no other options

def activate_memory_pressure(select_emp,gradient,over_count):
    proc_file = open("/proc/zoom/status","w")
    sum = 0
    if select_emp == 1:
        sum += 2
    if gradient == 1:
        sum += 4
    if over_count == 1:
        sum += 8
    write_val = 1 + sum
    write_string = "M " + str(write_val)
    proc_file.write(write_string)
    proc_file.close()

def deacticate_memory_pressure():
    proc_file = open("/proc/zoom/status","w")
    proc_file.write("M 0")
    proc_file.close()

def massage_summary(inputFile,outputFile):
    # open the input file
    summaryFile = open(inputFile,"r")
    # open the output file for write
    newSummary = open(outputFile,"w")
    # read the first line
    summary = summaryFile.readline()
    # immedidately write back the first line
    newSummary.write(summary)
    # read first numerical line
    summary = summaryFile.readline()
    #trim the input file of space
    summary = summary.lstrip()
    # get the initial time
    initTime = int(summary[0:10])
    while summary:
        # discontinue if end of the file
        if len(summary) < 10:
            break
        #trim the input file of space
        summary = summary.lstrip()
        # Compute the current time\
        time = str(int(summary[0:10]) - initTime)
        # modify the summary line
        summary = time + summary[10:]
        # write the new summary line to output
        newSummary.write(summary)
        # get the next line
        summary = summaryFile.readline()    
    summaryFile.close()
    newSummary.close()


# initial case
#deacticate_memory_pressure()
activate_memory_pressure(0,0,0)
work_output = 'work_' + case
work_command = './work 1000 R 1 4 > ' + work_output
print(work_command)
os.system(work_command)
raw_file = 'raw_output_' + case
summary_file = 'summary_output_' + case
monitor_command = './monitor ' + raw_file + ' ' + summary_file
print(monitor_command)
os.system(monitor_command) 
mod_file = 'mod_summary_' + case
print(summary_file)
massage_summary(summary_file,mod_file)







