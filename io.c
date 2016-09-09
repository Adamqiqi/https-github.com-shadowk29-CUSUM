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

void print_error_summary(FILE *logfile, int64_t *error_summary, int64_t numevents)
{
    int i;
    printf("\n\nEvent Summary: %"PRId64" events detected\n\nEvent Type\tCount\tPercentage\n\n",numevents);
    fprintf(logfile,"\n\nEvent Summary: %"PRId64" events detected\n\nEvent Type\tCount\tPercentage\n\n",numevents);
    for (i=0; i<NUMTYPES; i++)
    {
        printf("%d\t\t%"PRId64"\t%.3g %%\n",i,error_summary[i],100.0*error_summary[i]/(double)numevents);
        fprintf(logfile,"%d\t\t%"PRId64"\t%.3g %%\n",i,error_summary[i],100.0*error_summary[i]/(double)numevents);
    }
}

int64_t read_current(FILE *input, double *signal, void *rawsignal, int64_t position, int64_t length, int datatype, chimera *daqsetup)
{
    int64_t read = 0;
    if (datatype == 64)
    {
        read = read_current_double(input, signal, (uint64_t *) rawsignal, position, length);
    }
    else if (datatype == 16)
    {
        read = read_current_int16(input, signal, (uint16_t *) rawsignal, position, length);
    }
    else if (datatype == 0)
    {
        read = read_current_chimera(input, signal, (uint16_t *) rawsignal, position, length, daqsetup);
    }
    return read;
}

void chimera_gain(double *current, uint16_t *rawsignal, int64_t length, chimera *daqsetup)
{
    int64_t i;
    double closed_loop_gain = daqsetup->TIAgain*daqsetup->preADCgain;
    uint16_t bitmask = (uint16_t) ((1 << 16)-1) - (uint16_t) ((1 << (16-daqsetup->ADCbits)) - 1);
    uint16_t sample;
    for (i=0; i<length; i++)
    {
        current[i] = 0;
        sample = (uint16_t) (rawsignal[i] & bitmask);
        current[i] = daqsetup->ADCvref - (2.0 * daqsetup->ADCvref) * (double) sample / (double) (1<<16);
        current[i] = -current[i] / closed_loop_gain + daqsetup->currentoffset;
        current[i] *= 1e12;
    }
}

int64_t read_current_chimera(FILE *input, double *current, uint16_t *rawsignal, int64_t position, int64_t length, chimera *daqsetup)
{
    int64_t test;
    int64_t read = 0;

    if (fseeko64(input,(off64_t) position*sizeof(uint16_t),SEEK_SET))
    {
        return 0;
    }
    test = fread(rawsignal, sizeof(uint16_t), length, input);
    read = test;
    if (test != length)
    {
        perror("End of file reached");
    }

    chimera_gain(current, rawsignal, read, daqsetup);
    return read;
}

void swapByteOrder(double *current, uint64_t *rawsignal, int64_t length)
{
    union doublebits bitval;
    int64_t i;
    for (i=0; i<length; i++)
    {
        bitval.bits = rawsignal[2*i];
        bitval.bits = (bitval.bits >> 56) |
          ((bitval.bits<<40) & 0x00FF000000000000) |
          ((bitval.bits<<24) & 0x0000FF0000000000) |
          ((bitval.bits<<8) & 0x000000FF00000000) |
          ((bitval.bits>>8) & 0x00000000FF000000) |
          ((bitval.bits>>24) & 0x0000000000FF0000) |
          ((bitval.bits>>40) & 0x000000000000FF00) |
          (bitval.bits << 56);
        current[i] = bitval.currentval;
    }
}


int64_t read_current_double(FILE *input, double *current, uint64_t *rawsignal, int64_t position, int64_t length)
{
    int64_t test;

    int64_t read = 0;

    if (fseeko64(input,(off64_t) position*2*sizeof(double),SEEK_SET))
    {
        return 0;
    }
    test = fread(rawsignal, sizeof(uint64_t), 2*length, input);
    read = test/2;
    if (test != 2*length)
    {
        perror("End of file reached");
    }
    swapByteOrder(current, rawsignal, length);
    return read;
}


void swapByteOrder_int16(double *current, uint16_t *rawsignal, int64_t length)
{
    int64_t i;
    for (i=0; i<length; i++)
    {
        current[i] = (double) ((rawsignal[2*i]>>8)|(rawsignal[2*i]<<8));
    }
}


int64_t read_current_int16(FILE *input, double *current, uint16_t *rawsignal, int64_t position, int64_t length)
{
    int64_t test;

    int64_t read = 0;

    if (fseeko64(input,(off64_t) position*2*sizeof(uint16_t),SEEK_SET))
    {
        return 0;
    }
    test = fread(rawsignal, sizeof(uint16_t), 2*length, input);
    read = test/2;
    if (test != 2*length)
    {
        perror("End of file reached");
    }
    swapByteOrder_int16(current, rawsignal, length);
    return read;
}

void initialize_events_file(FILE *events, FILE *rate)
{
    fprintf(events,"id,\
type,\
start_time_s,\
event_delay_s,\
duration_us,\
threshold,\
baseline_before_pA,\
baseline_after_pA,\
effective_baseline_pA,\
area_pC,\
average_blockage_pA,\
relative_average_blockage,\
max_blockage_pA,\
relative_max_blockage,\
max_blockage_duration_us,\
n_levels,\
rc_const1_us,\
rc_const2_us,\
residual_pA,\
level_current_pA,\
level_duration_us,\
blockages_pA,\
stdev_pA\n");

fprintf(rate,"id,\
type,\
start_time_s,\
end_time_s\n");
}

void print_event_line(FILE *events, FILE *rate, event *current, double timestep, int64_t lasttime)
{
#ifdef DEBUG
    printf("Print Line\n");
    fflush(stdout);
#endif // DEBUG
    fprintf(rate,"%"PRId64",\
            %d,\
            %.6f,\
            %.6f\n",\
            current->index, \
            current->type, \
            current->start * timestep, \
            current->finish * timestep);

    if (current->type == CUSUM || current->type == STEPRESPONSE)
    {
        cusumlevel *level = current->first_level;
        fprintf(events,"%"PRId64",\
            %d,\
            %.6f,\
            %.6f,\
            %g,\
            %g,\
            %g,\
            %g,\
            %g,\
            %g,\
            %g,\
            %g,\
            %g,\
            %g,\
            %g,\
            %d,\
            %g,\
            %g,\
            %g,",\
            current->index, \
            current->type, \
            current->start * timestep, \
            (current->start - lasttime) * timestep, \
            current->length * timestep * 1e6, \
            current->threshold, \
            current->baseline_before, \
            current->baseline_after, \
            0.5 * (current->baseline_after + current->baseline_before),\
            current->area, \
            current->average_blockage, \
            d_abs(current->average_blockage / (0.5 * (current->baseline_before + current->baseline_after))), \
            current->max_blockage, \
            d_abs(current->max_blockage / (0.5 * (current->baseline_before + current->baseline_after))), \
            current->max_length * timestep * 1e6, \
            current->numlevels, \
            current->rc1 * timestep * 1e6, \
            current->rc2 * timestep * 1e6, \
            current->residual);
        while (level)
        {
            fprintf(events,"%g",level->current);
            if (level->next)
            {
                fprintf(events,";");
            }
            level = level->next;
        }
        fprintf(events,",");
        level = current->first_level;
        while (level)
        {
            fprintf(events,"%g",level->length * timestep * 1e6);
            if (level->next)
            {
                fprintf(events,";");
            }
            level = level->next;
        }
        fprintf(events,",");
        level = current->first_level;
        while (level)
        {
            fprintf(events,"%g",level->current-0.5*(current->baseline_after+current->baseline_before));
            if (level->next)
            {
                fprintf(events,";");
            }
            level = level->next;
        }
        fprintf(events,",");
        level = current->first_level;
        while (level)
        {
            fprintf(events,"%g",level->stdev);
            if (level->next)
            {
                fprintf(events,";");
            }
            level = level->next;
        }
        fprintf(events,"\n");
        fflush(events);
    }
}


void print_signal(event *current, int64_t length, char *filename, double timestep)
{
    FILE *output; //a test file for plotting output
    if ((output = fopen(filename,"w"))==NULL)
    {
        printf("Cannot open output file\n");
        exit(24);
    }
    int64_t i;
    if (current->type == STEPRESPONSE)
    {
        double stepfit;
        double u1 = current->first_level->length;
        double u2 = u1 + current->first_level->next->length;
        for (i=0; i<length; i++)
        {
            stepfit = current->first_level->current;
            if (i > u1)
            {
                stepfit -= (current->first_level->current-current->first_level->next->current)*(1-exp(-(i-u1)/current->rc1));
            }
            if (i > u2)
            {
                stepfit += (current->first_level->next->next->current-current->first_level->next->current)*(1-exp(-(i-u2)/current->rc2));
            }
            fprintf(output,"%g,%g,%g,%g\n",i*timestep,current->signal[i], current->filtered_signal[i],stepfit);
        }
    }
    else
    {
        for (i=0; i<length; i++)
        {
            fprintf(output,"%g,%g,%g\n",i*timestep,current->signal[i], current->filtered_signal[i]);
        }
    }
    fflush(output);
    fclose(output);
}

void print_event_signal(int64_t index, event *current, double timestep, char *eventsfolder)
{
#ifdef DEBUG
    printf("Print Signal\n");
    fflush(stdout);
#endif // DEBUG
    if (current->type == CUSUM || current->type == STEPRESPONSE)
    {
        char eventname[1024];
        sprintf(eventname,"%s/event_%08"PRId64".csv",eventsfolder,index);
        print_signal(current, current->length + current->padding_before + current->padding_after, eventname, timestep);
    }
}



void print_histogram(char *filename, histostruct *histogram)
{
    int64_t i;
    FILE *output;
    if ((output = fopen(filename, "w"))==NULL)
    {
        printf("Cannot open histogram file\n");
        exit(25);
    }
    for (i=0; i<histogram->numbins; i++)
    {
        fprintf(output,"%g,%g,%g,%g\n",(i+0.5)*histogram->delta+histogram->offset, histogram->histogram[i][0], histogram->histogram[i][1], histogram->histogram[i][2]);
    }
    fclose(output);
}





void configure_defaults(configuration *config)
{
    //deaults for config file, can be overwritten if provided
    config->start = 0;
    config->finish = 0;
    config->usefilter = 0;
    config->eventfilter = 0;
    config->binsize = 10;
    config->event_direction = 0;
    config->cusum_min_threshold = 0.1;
    config->cusum_max_threshold = 100.0;
    config->maxiters = 1000;
    config->stepfit_samples = 0;
    config->attempt_recovery = 0;
}

void config_sanity_check(configuration *config, FILE *logfile)
{
    printf("Verifying config parameters\nAny notes below will modify the config file above for the actual run\n\n");
    fprintf(logfile,"Verifying config parameters\nAny notes below will modify the config file above for the actual run\n\n");
    if (config->readlength < 2 * config->event_maxpoints)
    {
        printf("Readlength should be at least 2 times event_maxpoints. Correction:\nreadlength=%"PRId64"\n",2 * config->event_maxpoints);
        fprintf(logfile,"Readlength should be at least 2 times event_maxpoints. Correction:\nreadlength=%"PRId64"\n",2 * config->event_maxpoints);
        config->readlength = 2 * config->event_maxpoints;
    }

    if (config->order > 10)
    {
        printf("Bessel filters of order >10 are not supported. Correction:\npoles=10\n");
        fprintf(logfile,"Bessel filters of order >10 are not supported. Correction:\npoles=10\n");
        config->order = 10;
    }
    else if (config->order < 2)
    {
        printf("Bessel filters of order <2 are not supported. Correction:\npoles=2\n");
        fprintf(logfile,"Bessel filters of order <2 are not supported. Correction:\npoles=2\n");
        config->order = 2;
    }
    else if (config->order % 2 == 1)
    {
        printf("Bessel filters of order >10 are not supported. Correction:\npoles=%"PRId64"\n",config->order + 1);
        fprintf(logfile,"Bessel filters of order >10 are not supported. Correction:\npoles=%"PRId64"\n",config->order + 1);
        config->order += 1;
    }
    if (config->datatype==0 && config->samplingfreq != (int64_t) config->daqsetup->samplerate)
    {
        printf("Sampling rate does not match Chimera setup. Correction:\nsamplingfreq=%"PRId64"\n",config->samplingfreq);
        fprintf(logfile,"Sampling rate does not match Chimera setup. Correction:\nsamplingfreq=%"PRId64"\n",config->samplingfreq);
        config->samplingfreq = (int64_t) config->daqsetup->samplerate;
    }
    if (config->stepfit_samples > 0 && config->stepfit_samples < config->subevent_minpoints)
    {
        printf("Stepfit samples should be at least as large as subevent_minpoints. Correction:\nstepfit_samples=%"PRId64"\n",config->subevent_minpoints);
        fprintf(logfile,"Stepfit samples should be at least as large as subevent_minpoints. Correction:\nstepfit_samples=%"PRId64"\n",config->subevent_minpoints);
        config->stepfit_samples = config->subevent_minpoints;
    }
    if (config->stepfit_samples && !config->attempt_recovery)
    {
        printf("Stepfit_samples is on, but attempt_recovery is off. Correction:\nattempt_recovery=1\n");
        fprintf(logfile,"Stepfit_samples is on, but attempt_recovery is off. Correction:\nattempt_recovery=1\n");
        config->attempt_recovery = 1;
    }
    if (!config->stepfit_samples && config->attempt_recovery)
    {
        printf("Attempt_recovery is on, but step_fit samples is off. Correction:\nstepfit_samples=%"PRId64"\n",config->subevent_minpoints);
        fprintf(logfile,"Attempt_recovery is on, but step_fit samples is off. Correction:\nstepfit_samples=%"PRId64"\n",config->subevent_minpoints);
        config->stepfit_samples = config->subevent_minpoints;
    }
    if (!config->stepfit_samples && !config->attempt_recovery && config->event_minpoints < config->subevent_minpoints)
    {
        printf("Stepfit is off, and event_minpoints is less than subevent_minpoints. Correction:\nevent_minpoints=%"PRId64"\n",config->subevent_minpoints);
        fprintf(logfile,"Stepfit is off, and event_minpoints is less than subevent_minpoints. Correction:\nevent_minpoints=%"PRId64"\n",config->subevent_minpoints);
        config->event_minpoints = config->subevent_minpoints;
    }
    if (config->usefilter || config->eventfilter)
    {
        if (config->subevent_minpoints < 8.0/config->cutoff)
        {
            printf("Warning: subevent_minpoints is less than 4RC, levels might be underestimated. Suggest increaseing subevent_minpoints to %"PRId64"\nNo Correction\n",(int64_t) (8.0/config->cutoff));
            fprintf(logfile,"Warning: subevent_minpoints is less than 4RC, levels might be underestimated. Suggest increaseing subevent_minpoints to %"PRId64"\nNo Correction\n",(int64_t) (8.0/config->cutoff));
        }
        if (config->event_minpoints < 8.0/config->cutoff && config->stepfit_samples == 0)
        {
            printf("Warning: event_minpoints is less than 4RC, short events will not be fit accurately. Suggest increaseing event_minpoints to %"PRId64"\nNo Correction\n",(int64_t) (8.0/config->cutoff));
            fprintf(logfile,"Warning: event_minpoints is less than 4RC, short events will not be fit accurately. Suggest increaseing event_minpoints to %"PRId64"\nNo Correction\n",(int64_t) (8.0/config->cutoff));
        }
    }
    printf("\nDone config check\n\n");
    fprintf(logfile,"\nDone config check\n\n");
}


FILE * read_config(configuration *config, const char *version)
{
    configure_defaults(config);
    char configline[STRLENGTH];
    char *name;
    char *value;
    long int cutoff = 0;
    FILE *configfile;
    if ((configfile = fopen("config.txt","r"))==NULL)
    {
        printf("Cannot find config file: \"config.txt\"!");
        exit(27);
    }
    config->outputfolder[0]='\0';



    //initialize some defaults
    config->start = 0;
    config->usefilter = 0;
    config->eventfilter = 0;

    while ((fgets(configline, STRLENGTH, configfile)) != NULL)
    {
        name = strtok(configline,"=");
        value = strtok(NULL,"=\n");
        if (strcmp(name,"readlength") == 0)
        {
            config->readlength = strtoull(value,NULL,10);
        }
        else if (strcmp(name,"start") == 0)
        {
            config->start = strtoull(value,NULL,10);
        }
        else if (strcmp(name,"finish") == 0)
        {
            config->finish = strtoull(value,NULL,10);
        }
        else if (strcmp(name,"cutoff") == 0)
        {
            cutoff = strtol(value,NULL,10);
        }
        else if (strcmp(name,"samplingfreq") == 0)
        {
            config->samplingfreq = strtoull(value,NULL,10);
        }
        else if (strcmp(name,"poles") == 0)
        {
            config->order = strtoull(value,NULL,10);
        }
        else if (strcmp(name,"threshold") == 0)
        {
            config->threshold = strtod(value,NULL);
        }
        else if (strcmp(name,"hysteresis") == 0)
        {
            config->hysteresis = strtod(value,NULL);
        }
        else if (strcmp(name,"baseline_min") == 0)
        {
            config->baseline_min = strtod(value,NULL);
        }else if (strcmp(name,"baseline_max") == 0)
        {
            config->baseline_max = strtod(value,NULL);
        }
        else if (strcmp(name,"binsize") == 0)
        {
            config->binsize = strtod(value,NULL);
        }
        else if (strcmp(name,"cusum_min_threshold") == 0)
        {
            config->cusum_min_threshold = strtod(value,NULL);
        }
        else if (strcmp(name,"cusum_max_threshold") == 0)
        {
            config->cusum_max_threshold = strtod(value,NULL);
        }
        else if (strcmp(name,"cusum_delta") == 0)
        {
            config->cusum_delta = strtod(value,NULL);
        }
        else if (strcmp(name,"cusum_minstep") == 0)
        {
            config->cusum_minstep = strtod(value,NULL);
        }
        else if (strcmp(name,"event_minpoints") == 0)
        {
            config->event_minpoints = strtoull(value,NULL,10);
        }
        else if (strcmp(name,"event_maxpoints") == 0)
        {
            config->event_maxpoints = strtoull(value,NULL,10);
        }
        else if (strcmp(name,"subevent_minpoints") == 0)
        {
            config->subevent_minpoints = strtoull(value,NULL,10);
        }
        else if (strcmp(name,"event_direction") == 0)
        {
            config->event_direction = strtol(value,NULL,10);
        }
        else if (strcmp(name,"use_filter") == 0)
        {
            config->usefilter = strtol(value,NULL,10);
        }
        else if (strcmp(name,"event_filter") == 0)
        {
            config->eventfilter = strtol(value,NULL,10);
        }
        else if (strcmp(name,"input_file") == 0)
        {
            strncpy(config->filepath,value,STRLENGTH-1);
            config->filepath[STRLENGTH-1]='\0';
        }
        else if (strcmp(name,"output_folder") == 0)
        {
            strncpy(config->outputfolder,value,STRLENGTH-1);
            config->outputfolder[STRLENGTH-1]='\0';
        }
        else if (strcmp(name,"datatype") == 0)
        {
            config->datatype = strtol(value,NULL,10);
        }
        else if (strcmp(name,"stepfit_samples") == 0)
        {
            config->stepfit_samples = strtol(value,NULL,10);
        }
        else if (strcmp(name,"maxiters") == 0)
        {
            config->maxiters = strtol(value,NULL,10);
        }
        else if (strcmp(name,"attempt_recovery") == 0)
        {
            config->attempt_recovery = strtol(value,NULL,10);
        }
        else if (strcmp(name,"SETUP_ADCSAMPLERATE") == 0)
        {
            config->daqsetup->samplerate = strtod(value,NULL);
        }
        else if (strcmp(name,"SETUP_pAoffset") == 0)
        {
            config->daqsetup->currentoffset = strtod(value,NULL);
        }
        else if (strcmp(name,"SETUP_TIAgain") == 0)
        {
            config->daqsetup->TIAgain = strtod(value,NULL);
        }
        else if (strcmp(name,"SETUP_ADCVREF") == 0)
        {
            config->daqsetup->ADCvref = strtod(value,NULL);
        }
        else if (strcmp(name,"SETUP_ADCBITS") == 0)
        {
            config->daqsetup->ADCbits = strtol(value,NULL,10);
        }
        else if (strcmp(name,"SETUP_preADCgain") == 0)
        {
            config->daqsetup->preADCgain = strtod(value,NULL);
        }
    }

    if (config->usefilter == 0 && config->eventfilter == 0)
    {
        config->order = 0;
        config->cutoff = 0;
    }
    else
    {
        config->cutoff = 2.0 *(double) cutoff/(double) config->samplingfreq;
    }


    int test;
    if (config->outputfolder[0] == '\0' || config->outputfolder[0] == '\n')
    {
        if ((test=snprintf(config->outputfolder,STRLENGTH-1,"output")) < 0 || test >= STRLENGTH)
        {
            printf("Cannot write default output folder string\n");
            exit(1);
        }
    }
    if ((test=snprintf(config->eventsfolder,STRLENGTH-1,"%s/events",config->outputfolder)) < 0 || test >= STRLENGTH)
    {
        printf("Cannot write eventsfolder string\n");
        exit(1);
    }
    if ((test=snprintf(config->eventsfile,STRLENGTH-1,"%s/events.csv",config->outputfolder)) < 0 || test >= STRLENGTH)
    {
        printf("Cannot write eventsfile string\n");
        exit(1);
    }
    if ((test=snprintf(config->ratefile,STRLENGTH-1,"%s/rate.csv",config->outputfolder)) < 0 || test >= STRLENGTH)
    {
        printf("Cannot write ratefile string\n");
        exit(1);
    }
    if ((test=snprintf(config->logfile,STRLENGTH-1,"%s/summary.txt",config->outputfolder)) < 0 || test >= STRLENGTH)
    {
        printf("Cannot write logfile string\n");
        exit(1);
    }

    FILE *logfile;
    logfile = fopen64_and_check(config->logfile,"w", 4);
    printf("Using CUSUM version %s\n",version);
    fprintf(logfile,"Using CUSUM version %s\n",version);
    fseek(configfile,0,SEEK_SET);
    fprintf(logfile, "<----CONFIGURATION BEGINS---->\n\n");
    while ((fgets(configline, STRLENGTH, configfile)) != NULL)
    {
        fprintf(logfile, "%s", configline);
    }
    fprintf(logfile, "<----CONFIGURATION ENDS---->\n\n");
    config_sanity_check(config, logfile);
    fclose(configfile);
    return logfile;
}

