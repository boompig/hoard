#!/usr/bin/perl

use strict;

# Check for correct usage
if (@ARGV != 2) {
  print "usage: runtests.pl <dir> <iters>\n";
  print "    where <dir> is the directory containing the test executable and\n";
  print "    Results subdirectory, and <iters> is the number of trials to perform.\n";
  die;
}

my $dir = $ARGV[0];
my $iters = $ARGV[1];

#Ensure existence of $dir/Results
if (!-e "$dir/Results") {
    mkdir "$dir/Results", 0755
	or die "Cannot make $dir/Results: $!";
}

# Initialize list of allocators to test.
my @namelist = ("a2alloc");
#my @namelist = ("kheap");
#my @namelist = ("a2alloc", "kheap");

#my $max_threads = 1;
my $max_threads = 8;
my $name;

foreach $name (@namelist) {
    print "name = $name\n";
    # Create subdirectory for current allocator results
    if (!-e "$dir/Results/$name") {
        mkdir "$dir/Results/$name", 0755
            or die "Cannot make $dir/Results/$name: $!";
    }

    # Run tests for 1 to 8 threads
    for (my $i = 1; $i <= $max_threads; $i++) {
        print "Thread $i\n";
        my $cmd1 = "echo \"\" > $dir/Results/$name/larson-$i";
        system "$cmd1";
        for (my $j = 1; $j <= $iters; $j++) {
            print "Iteration $j\n";
            #my $cmd = "$dir/larson-$name < $dir/Input/small-$i-threads >> $dir/Results/$name/larson-$i 2>&1";
            my $cmd = "$dir/larson-$name < $dir/Input/small-$i-threads";
            print "$cmd\n";
            system "$cmd";
        }
    }
}


