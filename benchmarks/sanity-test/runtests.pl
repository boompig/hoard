#!/usr/bin/perl

use strict;

# Check for correct usage
if (@ARGV != 1) {
  print "usage: runtests.pl <dir>\n";
  print "    where <dir> is the directory containing the test executable and\n";
  print "    Results subdirectory.\n";
  die;
}

my $dir = $ARGV[0];

#Ensure existence of $dir/Results
if (!-e "$dir/Results") {
    mkdir "$dir/Results", 0755
	or die "Cannot make $dir/Results: $!";
}

# Initialize list of allocators to test.
#my @namelist = ("libc", "kheap", "a2alloc");
#my @namelist = ("libc", "kheap");
#my @namelist = ("kheap", "a2alloc");
my @namelist = ("a2alloc");

my $max_threads = 1;
#my $max_threads = 8;
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
        my $cmd1 = "echo \"\" > $dir/Results/$name/sanity-test-$i";
        system "$cmd1";
        #my $cmd = "$dir/sanity-test-$name < $dir/Input/small-$i-threads >> $dir/Results/$name/sanity-test-$i 2>&1";
        my $cmd = "$dir/sanity-test-$name < $dir/Input/small-$i-threads";
        print "$cmd\n";
        system "$cmd";
    }
}


