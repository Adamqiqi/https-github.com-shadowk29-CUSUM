/*

                                COPYRIGHT
    Copyright (C) 2015-2016 Kyle Briggs (kbrig035<at>uottawa.ca)

    This file is part of CUSUM.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include"io.h"
#include"utils.h"
#include"detector.h"
#include"bessel.h"
#include"stepfit.h"
#define _VERSION_ "3.1.3"

int main()
{
    if (!(sizeof(double) * CHAR_BIT == 64))
    {
        printf("CUSUM requires 64-bit doubles\nPlease recompile with an appropriate compiler\n");
        exit(-1);
    }

    //read the configuration file
    configuration *config;
    config = calloc_and_check(1,sizeof(configuration),"Cannot allocate config struct");
    config->daqsetup = calloc_and_check(1,sizeof(chimera),"Cannot allocate DAQ struct");

    FILE *logfile;
    logfile = read_config(config, _VERSION_);


    if (config->datatype != 16 && config->datatype != 64 && config->datatype !=0)
    {
        printf("datatype currently can only be 0, 16, or 64\n");
        exit(43);
    }

    //open the input file

    FILE **input;
    #pragma omp parallel
    {
        #pragma omp master
        {
            input = calloc_and_check(omp_get_num_threads(), sizeof(FILE *), "Cannot open input file\n");
        }
        #pragma omp barrier
        input[omp_get_thread_num()] = fopen64_and_check(config->filepath,"rb", 4);
    }


    FILE *events;
    events = fopen64_and_check(config->eventsfile,"w",21);

    FILE *rate;
    rate = fopen64_and_check(config->ratefile,"w",21);

    FILE *baselinefile = NULL;
    //baselinefile = fopen64_and_check(config->baselinefile,"w",21);

    initialize_events_file(events, rate, baselinefile);

    bessel *lpfilter = NULL;

    int64_t *error_summary = calloc_and_check(NUMTYPES, sizeof(int64_t), "Cannot allocate error array");

    //initialize the low-pass filter and allocate necessary memory
    int64_t filterpadding = 0;
    if (config->usefilter || config->eventfilter)
    {
        filterpadding = 100 * 2.0 / config->cutoff;
        lpfilter = initialize_filter(lpfilter, config->order, config->cutoff, config->readlength, filterpadding);
    }
    //allocate memory for file reading


    edge **head_edge;
    edge **current_edge;
    double **paddedsignal;
    double **signal;
    void **rawsignal;
    baseline_struct **baseline_stats;
    #pragma omp parallel
    {
        #pragma omp master
        {
            paddedsignal = calloc_and_check(omp_get_num_threads(), sizeof(double *), "Cannot allocate paddedsignal");
            signal = calloc_and_check(omp_get_num_threads(), sizeof(double *), "Cannot allocate signal");
            rawsignal = calloc_and_check(omp_get_num_threads(), sizeof(void *), "Cannot allocate raw");
            baseline_stats = calloc_and_check(omp_get_num_threads(), sizeof(baseline_struct *), "Cannot allocate raw");
            head_edge = calloc_and_check(omp_get_num_threads(), sizeof(edge *), "Cannot allocate head edge");
            current_edge = calloc_and_check(omp_get_num_threads(), sizeof(edge *), "Cannot allocate current edge");
        }
        #pragma omp barrier
        paddedsignal[omp_get_thread_num()] = calloc_and_check(config->readlength + 2*(config->order + filterpadding),sizeof(double), "Cannot allocate file reading signal array");
        signal[omp_get_thread_num()] = &paddedsignal[omp_get_thread_num()][config->order + filterpadding];
        baseline_stats[omp_get_thread_num()] = NULL;
        baseline_stats[omp_get_thread_num()] = initialize_baseline(baseline_stats[omp_get_thread_num()], config);
        rawsignal[omp_get_thread_num()] = NULL;
        switch (config->datatype)
        {
            case 0:
                rawsignal[omp_get_thread_num()] = calloc_and_check(config->readlength, sizeof(uint16_t), "Cannot allocate chimera rawsignal array");
                break;
            case 16:
                rawsignal[omp_get_thread_num()] = calloc_and_check(config->readlength, 2*sizeof(uint16_t), "Cannot allocate f2 rawsignal array");
                break;
            case 64:
                rawsignal[omp_get_thread_num()] = calloc_and_check(config->readlength, 2*sizeof(uint64_t), "Cannot allocate f8 rawsignal array");
                break;
        }
        head_edge[omp_get_thread_num()] = initialize_edges();
        current_edge[omp_get_thread_num()] = head_edge[omp_get_thread_num()];
    }




    //find out how big the file is for use in a progressbar
    int64_t filesize = get_filesize(input[0], config->datatype);
    if (config->finish == 0)
    {
        config->finish = filesize;
    }
    else
    {
        config->finish = filesize < config->finish ? filesize : config->finish;
    }


    //initialize linked list to store the locations of edges in the input file


    //initialize struct to store the information about events found using the edge list
    event *current_event;
    current_event = initialize_events();

    fprintf(logfile, "<----RUN LOG BEGINS---->\n\n");
    printf("Locating events... \n");
    fprintf(logfile, "Locating events...\n ");
    fflush(stdout);

    double risetime;
    if (config->eventfilter || config->usefilter)
    {
        risetime = 2.0/config->cutoff;
    }
    else
    {
        risetime = 5;
    }
    int64_t typeswitch = 0;
    double baseline;
    double badbaseline = 0;
    double goodbaseline = 0;
    int64_t readlength = config->readlength;
    int64_t i;
    int64_t read;
    int64_t pos;
    pos = 0;
    char progressmsg[STRLENGTH];
    time_t start_time;
    time_t curr_time;
    time(&start_time);
    int64_t blocknum;
    int tid;
    int64_t numedges;
    #pragma omp parallel private(blocknum, read, baseline, tid, numedges)
    {
        read = 0;
        tid = omp_get_thread_num();
        #pragma omp for reduction(+:goodbaseline, badbaseline)
        for (pos = config->start; pos < config->finish; pos += readlength)
        {
            blocknum = pos / readlength;
            //snprintf(progressmsg,STRLENGTH*sizeof(char)," %g seconds processed",(pos-config->start)/(double) config->samplingfreq);
            //progressbar(pos-config->start,config->finish-config->start,progressmsg,difftime(time(&curr_time),start_time));
            read = read_current(input[tid], signal[tid], rawsignal[tid], pos, intmin(config->readlength,config->finish - pos), config->datatype, config->daqsetup);
            if (config->usefilter)
            {
                filter_signal(signal[tid], paddedsignal[tid], lpfilter, read, tid);
            }
            gauss_histogram(signal[tid], baseline_stats[tid], read);
            baseline = baseline_stats[tid]->mean;
            if (isnan(baseline_stats[tid]->mean) || isnan(baseline_stats[tid]->stdev))
            {
                baseline = 0;
            }
            //output_baseline_stats(baselinefile, baseline_stats[omp_get_thread_num()], pos, config->samplingfreq);
            if (baseline < config->baseline_min || baseline > config->baseline_max)
            {
                badbaseline += read;
            }
            else
            {
                goodbaseline += read;
                current_edge[tid] = detect_edges(signal[tid], baseline, read, current_edge[tid], config->threshold, baseline_stats[tid]->stdev, config->hysteresis, pos, config->event_direction, blocknum, tid);
            }
            //printf("Thread %d found %g and %g\n",tid,baseline_stats[tid]->mean, baseline_stats[tid]->stdev);
            memset(signal[tid],'0',(config->readlength)*sizeof(double));
        }
        current_edge[tid] = head_edge[tid];

        //printf("Thread %d first edge at %g from block %"PRId64"\n",tid,current_edge[tid]->location/(double) config->samplingfreq, current_edge[tid]->blocknum);
        while (current_edge[tid]->next)
        {
            printf("Thread %d edge at %g from block %"PRId64"\n",tid,current_edge[tid]->location/(double) config->samplingfreq, current_edge[tid]->blocknum);
            numedges++;
            current_edge[tid] = current_edge[tid]->next;
        }
        numedges++;
        printf("\nThread %d found %"PRId64"\n",tid,numedges);

        free(paddedsignal[tid]);
    }
    snprintf(progressmsg,STRLENGTH*sizeof(char)," %g seconds processed",(pos-(readlength-read)-config->start)/(double) config->samplingfreq);
    progressbar(pos -config->start,config->finish-config->start,progressmsg,difftime(time(&curr_time),start_time));
    printf("\nRead %g seconds of good baseline\nRead %g seconds of bad baseline\n", goodbaseline/(double) config->samplingfreq, badbaseline / (double) config->samplingfreq);
    fprintf(logfile, "\nRead %g seconds of good baseline\nRead %g seconds of bad baseline\n", goodbaseline/(double) config->samplingfreq, badbaseline / (double) config->samplingfreq);
    free(paddedsignal);
    exit(999);


    //fclose(baselinefile);

/*
    int64_t index = 0;
    int64_t numevents = 0;
    int64_t edgecount;
    int64_t edgenum = 0;
    int64_t edges;


    edgecount = count_edges(current_edge);
    current_edge = head_edge;


    int64_t lasttime = config->start;
    int64_t last_end = config->start;
    printf("Processing %"PRId64" edges\n", edgecount);
    time(&start_time);
    while (current_edge)
    {
#ifdef DEBUG
    printf("Main Loop\n");
    fflush(stdout);
#endif // DEBUG
        snprintf(progressmsg,STRLENGTH*sizeof(char)," %"PRId64" events processed",numevents);
        progressbar(edgenum, edgecount, progressmsg,difftime(time(&curr_time),start_time));
        edges = get_next_event(current_event, current_edge, index);
        edgenum += edges;
        for (i=0; i<edges; i++)
        {
            current_edge = current_edge->next;
        }
        index++;
        identify_step_events(current_event, config->stepfit_samples, config->subevent_minpoints, config->attempt_recovery);
        filter_long_events(current_event, config->event_maxpoints);
        filter_short_events(current_event, config->event_minpoints);
        generate_trace(input[0], current_event, config->datatype, rawsignal, logfile, lpfilter, config->eventfilter, config->daqsetup, current_edge, last_end, config->start, config->subevent_minpoints);
        last_end = current_event->finish;
        cusum(current_event, config->cusum_delta, config->cusum_min_threshold, config->cusum_max_threshold, config->subevent_minpoints);
        typeswitch += average_cusum_levels(current_event, config->subevent_minpoints, config->cusum_minstep, config->attempt_recovery);
        step_response(current_event, risetime, config->maxiters, config->cusum_minstep);
        populate_event_levels(current_event);
        calculate_level_noise(current_event, config->subevent_minpoints);
        refine_event_estimates(current_event);
        event_baseline(current_event, config->baseline_min, config->baseline_max);
        event_max_blockage(current_event);
        event_area(current_event, 1.0/config->samplingfreq);
        print_event_signal(current_event->index, current_event, 1.0/config->samplingfreq*SECONDS_TO_MICROSECONDS,config->eventsfolder);
        print_event_line(events, rate, current_event, 1.0/config->samplingfreq, lasttime);
        lasttime = current_event->start;
        current_edge = current_edge->next;
        edgenum++;
        numevents++;
        error_summary[current_event->type]++;
        free_single_event(current_event);
#ifdef DEBUG
    printf("Done\n");
    fflush(stdout);
#endif // DEBUG
    }
    snprintf(progressmsg,STRLENGTH*sizeof(char)," %"PRId64" events processed",numevents);
    progressbar(edgenum, edgecount, progressmsg,difftime(time(&curr_time),start_time));

    print_error_summary(logfile, error_summary, numevents);


    printf("\nCleaning up memory usage...\n");
    fprintf(logfile, "Cleaning up memory usage...\n");
    free(current_event);
    free_edges(head_edge);
    #pragma omp parallel
    {
        fclose(input[omp_get_thread_num()]);
    }
    free(input);
    free(config->daqsetup);
    free(config);
    free(error_summary);
    #pragma omp parallel
    {
        free(rawsignal[omp_get_thread_num()]);
        free_baseline(baseline_stats[omp_get_thread_num()]);
    }
    free(rawsignal);
    free(baseline_stats);


    if (config->usefilter || config->eventfilter)
    {
        free_filter(lpfilter);
    }


    fprintf(logfile, "<----RUN LOG ENDS---->\n\n");
    fclose(logfile);
    fclose(events);
    fclose(rate);
    system("pause");
    return 0;*/
}
