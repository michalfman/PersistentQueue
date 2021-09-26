#!/usr/bin/python
import subprocess
import matplotlib as mpl
from matplotlib import pyplot as plt
from matplotlib import ticker
import numpy as np
import re
import sys

def plot_speeds(speeds, errors, dataset):
    average_speeds = speeds
    indexes = [1, 2, 3, 4, 5, 6, 7, 8]
    MS = 12
    LW = 4
    
    
    #-------------------------------------------AllQueues------------------------------------------------------------#
    plt.figure(2)
    
    if("Test MSQueue " in average_speeds):
        plt.plot(indexes, average_speeds["Test MSQueue "],'-o', label="$MSQ$", markersize=MS, linewidth=3, c="black")
    if("Test Durable " in average_speeds):
        plt.plot(indexes, average_speeds["Test Durable "],'-*',label="$Durable$", markersize=MS,  linewidth=3, c="blue")
    if("Test Log " in average_speeds):
        plt.plot(indexes, average_speeds["Test Log "],'-^', label="$Log$", markersize=MS, linewidth=3, c="red")
    if("Test Relaxed 0 size 1000000\n" in average_speeds):
        plt.plot(indexes, average_speeds["Test Relaxed 0 size 5\n"],"-D",label="$Relaxed\ " "10\ " "size\ " "1000000$", markersize=MS, linewidth=LW, c="gold")
    if("Test Relaxed 00 size 1000000\n" in average_speeds):
        plt.plot(indexes, average_speeds["Test Relaxed 00 size 5\n"],"-v",label="$Relaxed\ " "100\ " "size\ " "1000000$", markersize=MS, linewidth=LW, c="green")
    if("Test Relaxed 000 size 1000000\n" in average_speeds):
        plt.plot(indexes, average_speeds["Test Relaxed 000 size 5\n"],"-H",label="$Relaxed\ " "1000\ " "size\ " "1000000$", markersize=MS, linewidth=LW, c="purple")


    ticks,labels = plt.xticks()
    plt.xlabel("Num of Threads", fontsize=24)
    ylabel_str = "Operations/Second [Millions]" #For Queries in Millions
    plt.ylabel(ylabel_str, fontsize=24)
    plt.tick_params(labelsize=20)
    plt.legend(loc="best", fontsize = 'xx-large') # keys of the graphs
    plt.ticklabel_format(style='sci', axis='y')
    scale_y = 1e6
    ticks_y = ticker.FuncFormatter(lambda x, pos: '{0:g}'.format(x/scale_y))
    plt.gca().yaxis.set_major_formatter(ticks_y)
    plt.tight_layout()
    plt.savefig('AllQueues.png')
    plt.clf()



csvFiles = dict()
csvFiles['Results'] = sys.argv[1]

speeds = dict()
errors = dict()


for dataset in csvFiles:
    lines = open(csvFiles[dataset]).readlines()
    speeds[dataset] = dict()
    errors[dataset] = dict()
    sumT = 0
    lineNumber = 0
    alg = ""
    f = ""
    size = ""
    i = 0
    t_n_1 = 2.262 # t statistic for 9 degrees of freedom @ 0.95 confidence two tail
    for line in lines:
        if (lineNumber % 11 == 0):
            sumT = 0
            lineSplitted = line.split('-')
            alg = lineSplitted[0]
            values=[]
            i = 0
            if (alg == "Test Relaxed "):
                f = lineSplitted[1].split(':')[2]
                size = lineSplitted[1].split(':')[3]
                f = f.split(' ')[1]
                f = f[1:]
                size = size[1:]
                alg = alg + f + " size " + size
            if (alg not in speeds[dataset]):
                speeds[dataset][alg]=[]
                errors[dataset][alg]=[]

            lineNumber+=1
        else:
            sumT += int(line)
            lineNumber+=1
            values.append(int(line))
            i+=1
            if (lineNumber % 11 == 0):                      #last result
                averageT = float(sumT/10)
                speeds[dataset][alg].append(averageT)       #millions
                sos = sum([(y - averageT)**2 for y in values])
                variance = (sos / (len(values) - 1) )# ** 0.5
                err = variance * t_n_1 / pow(len(values), 0.5)
                errors[dataset][alg].append(err)
    plot_speeds(speeds[dataset], errors[dataset], dataset)







