#!/usr/local/bin/perl

# Expect 1 argument to be cdf login name of submitter

use strict;

my $graphtitle = "Sanity Test throughput";

#my @namelist = ("libc", "kheap", "a2alloc");
#my @namelist = ("libc", "a2alloc");
my @namelist = ("a2alloc");
my %names;

# This allows you to give each series a name on the graph
# that is different from the file or directory names used
# to collect the data.  We happen to be using the same names.
$names{"libc"} = "libc";
$names{"kheap"} = "kheap";
$names{"a2alloc"} = $ARGV[0];


my $name;
my $base;
my %scalability;
my %fragmentation;
my @reqd_mem = (240000, 480000, 720000, 960000,
		1200000, 1440000, 1680000, 1920000);

my $nthread = 8;

foreach $name (@namelist) {
    open G, "> Results/$name/data";
    $base = 0;
    $scalability{$name} = 0;
    $fragmentation{$name} = 0;
    for (my $i = 1; $i <= $nthread; $i++) {
	open F, "Results/$name/larson-$i";
	my $total = 0;
	my $count = 0;
	my $min = 1e30;
	my $max = -1e30;
	my $mem_min = 1e30;
	my $mem_max = -1e30;
	my $mem_total = 0;

	while (<F>) {
	    chop;
	    # Throughput results
	    if (/Throughput =\s+([0-9]+)\s+/) {
		# print G "$i\t$1\n";
		my $current = $1;
		$total += $1;
		$count++;
		if ($current < $min) {
		    $min = $current;
		}
		if ($current > $max) {
		    $max = $current;
		}
	    }
	    # Memory usage results
	    if (/Memory used =\s+([0-9]+)\s+/) {
	      my $current = $1;
	      $mem_total += $1;
	      if ($current < $mem_min) {
		$mem_min = $current;
	      }
	      if ($current > $mem_max) {
		$mem_max = $current;
	      }
	    }
	}
	if ($count > 0) {
	    my $avg = $total / $count;
	    print G "$i\t$avg\t$min\t$max\n";
	    $avg = $mem_total / $count;
	    print "i = $i, memory used: $avg avg, $mem_min min, $mem_max max\n";
	    $fragmentation{$name} += $avg / $reqd_mem[$i-1];

	    if ($i == 1) {
		$base = $max; # For throughput, higher is better, take max
	    } elsif ($i < $nthread) {	# don't count max thread case
		my $speedup = $max / $base;
		$scalability{$name} += $speedup / $i;
	    }
	} else {
	    print "oops count is zero, $name, larson-$i\n";
	}

	close F;
    }
    close G;
}

open PLOT, "|gnuplot";
print PLOT "set term postscript\n";
print PLOT "set output \"larson.ps\"\n";
print PLOT "set title \"$graphtitle\"\n";
print PLOT "set ylabel \"Throughput (memory ops per second)\"\n";
print PLOT "set xlabel \"Number of threads\"\n";
print PLOT "set xrange [0:17]\n";
print PLOT "plot ";

foreach $name (@namelist) {
    $scalability{$name} /= 6.0;
    $fragmentation{$name} /= $nthread;
    printf "name = $name\n\tscalability score %.3f\n",$scalability{$name};
    printf "\tfragmentation score = %.3f\n\n",$fragmentation{$name};
    my $titlename = $names{$name};
    if ($name eq $namelist[-1]) {
	print PLOT "\"Results/$name/data\" title \"$titlename\" with linespoints\n";
    } else {
	print PLOT "\"Results/$name/data\" title \"$titlename\" with linespoints,";
    }
}
close PLOT;


