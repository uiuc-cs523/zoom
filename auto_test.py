#!/usr/bin/python3

import os

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
os.system('./work 500 R 1 4')
os.system('./monitor raw_output summary_output')
massage_summary('summary_output','mod_summary')







