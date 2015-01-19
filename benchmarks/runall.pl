#!/usr/bin/perl

use strict;

# Check for correct usage
if (@ARGV != 1) {
    print "usage: runall.pl <dir>\n";
    print "    where <dir> is the directory containing the benchmark subdirectories\n";
    die;
}

my $dir=$ARGV[0];
my $name;
my $iters = 5;

foreach $name ("cache-scratch", "cache-thrash", "threadtest", "larson", "linux-scalability") {
  print "name = $name\n";
  print "running $dir/$name/runtests.pl";
  system "$dir/$name/runtests.pl $dir/$name $iters";
  
}


