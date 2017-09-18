import argparse
import gzip

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

def graph_timeline(timeline, filename, log=False, limit=10, exclude=None):
    """
    Graph a memory timeline.

    Args:
        timeline (list): A list of (time,{scope:value}) tuples.
        filename (str): A filename to write to.
        log (bool): Make the y-axis log scale.
        limit (int): Number of lines to display (from highest to lowest).
        exclude (iterable): Iterable of names to exclude.
    """

    if not exclude:
        exclude = {}

    series = {}
    for t,data in timeline:
        for k in data:
            if k in exclude:
                continue
            if k not in series:
                series[k] = {'times':[],'values':[],'label':k}
            series[k]['times'].append(t)
            series[k]['values'].append(data[k])
    highest_series = sorted(series,key=lambda k:max(series[k]['values']),reverse=True)[:limit]

    plot([series[k] for k in highest_series], filename, log=log)

def plot(series, filename, log=False):
    """
    Plot a series to file.

    Args:
        series (list): A sorted series, from max value to low value.
        filename (str): Output filename.
    """
    fig = plt.figure()
    ax = fig.add_subplot(111)
    ax.set_ylabel('Memory (MB)')
    ax.set_xlabel('Time (s)')

    max_mem = max(series[0]['values'])
    ax.set_ylim([1 if log else 0,int(max_mem*1.1)])

    plot_func = getattr(ax,'semilogy') if log else getattr(ax,'plot')
    for s in series:
        plot_func(s['times'],s['values'],label=s['label'],linewidth=1)
    
    # Shrink current axis's height by 20% on the bottom
    #box = ax.get_position()
    #ax.set_position([box.x0, box.y0 + box.height * 0.4,
    #                 box.width, box.height * 0.6])

    # Put a legend below current axis
    #lgd = ax.legend(loc='upper center', bbox_to_anchor=(0.5, -0.05),
    #                fancybox=False, shadow=False, ncol=1)
    lgd = ax.legend(bbox_to_anchor=(1.05, 1), loc=2, borderaxespad=0.)

    plt.savefig(filename, dpi=300, bbox_extra_artists=(lgd,), bbox_inches='tight')

def import_data(filename):
    """
    Import data from gzipped file.

    Args:
        filename (str): Name of gzipped file.

    Returns:
        list: A list of (time,{scope:value}) tuples.
    """
    ret = []
    t = 0
    time_series = {}
    if filename.endswith('.gz'):
        file_open = gzip.open
    else:
        file_open = open
    with file_open(filename, 'rb') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            if line.startswith('---'): # time code in microseconds
                if time_series:
                    ret.append((t,time_series))
                    time_series = {}
                t = float(line[3:])/1000000.0
                continue
            scope,value = line.rsplit('|',1) # scope, memory in bytes
            time_series[scope] = float(value)/1000000.0
    if time_series:
        ret.append((t,time_series))
    return ret

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('filename', type=str, help='input file to process')
    parser.add_argument('--outfile', default=None, type=str, help='outfile name')
    parser.add_argument('--log', action='store_true', help='plot in log scale')
    parser.add_argument('--limit', type=int, default=15, help='top # entries')
    args = parser.parse_args()

    data = import_data(args.filename)

    outfile_name = args.outfile if args.outfile else args.filename.replace('.gz','')+'.png'
    graph_timeline(data, outfile_name, log=args.log, limit=args.limit)
