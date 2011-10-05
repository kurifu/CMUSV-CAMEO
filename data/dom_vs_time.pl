#!/usr/bin/perl

use strict;

open(INFILE, "< 2011_09_21_hangman.log") or die "Couldn't open input file!";
open(OUTFILE, "> results.dat") or die "Couldn't create output file!";

print "Hello!\n";

while(<INFILE>) {
	print "Line is " . $_ ;
	($_);
	print OUTFILE "Line is " . $_;
}

close(OUTFILE);
close(INFILE);


sub getDate {
	my $line = $_[0];
	if(/(\d)(\d)?:(\d)(\d)?:(\d)(\d)_/) {
		print "Found a timestamp!\n";
	}
}
