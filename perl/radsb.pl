#!/usr/bin/perl

# for decoders with SPRUT firmware

use strict;
#use warnings;

use Time::HiRes qw(time);

# should run stty on the device first.

my $devpath;
my $init = 1;

foreach my $arg (@ARGV) {
	if ($arg =~ /^-I/) { $init = 0; }
	else { $devpath = $arg; }
}
die "need device name" unless defined $devpath;

my $dev;
if ($init) {
	# always reset the device on startup to clear it out. If it's been plugged into
	# a USB hub, it may still be running even if it's just been plugged back into
	# the host.
	open($dev, "+>", $devpath) || die "unable to open $devpath for reading: $?";
	print STDERR "resetting...";
	syswrite($dev, "#FF\n", 4);
	close $dev;

	# the reset will drop it off the bus for a bit; wait up to 10s for it to
	# come back.
	for (my $i = 0; $i < 10; $i++) {
		sleep 1; # sleep first in case it hasn't disappeared yet
		print STDERR "$i ";
		last if -e $devpath;
	}
	print STDERR "\n";

	open($dev, "+>", $devpath) || die "unable to open $devpath for reading: $?";
	syswrite($dev, "#00\n", 4);
	my $ver = <$dev>; chomp $ver;
	print STDERR "$ver\n";
	die "unknown version" unless $ver =~ /^\#00-00-06-04/;

	syswrite($dev, "#43-32\n", 7);
	my $mode = <$dev>; chomp $mode;
	print STDERR "$mode\n";
	die "SET_MODE failed" unless $mode =~ /^\s*\#43-32-(00-){14}/;

} else { # assume it's already in the right mode, at user request.
	open($dev, "<", $devpath) || die "unable to open $devpath for reading: $?";
}

$| = 1;

my $seqnum = 0;
my $lastdiff = undef;
while (my $line = <$dev>) {
	my $rx = time + 0;
	chomp $line; $line =~ s/^\s*(.*?)\s*$/$1/;
	die "not timestamped!" if ($line =~ s/^\*(.*)\;$/$1/);
	next unless ($line =~ /^\@([0-9A-F]{12})([0-9A-F]{14}|[0-9A-F]{28})\;(\#([0-9A-F]{8})\;|)$/);
	my ($tc, $sq, $fn) = (hex($1), $2, hex($4));

	# rxtime	correctedtime	ourseqnum	pictime	picserial	*data;
	print "" . sprintf("%.05f", $rx);
	print "\t" . sprintf("%.05f", $rx); # well, not corrected here.
	print "\t" . sprintf("%20.0d", $seqnum);
	print "\t" . sprintf("%20d", $tc);
	print "\t" . sprintf("%20d", $fn);
	print " *$sq;\n";
	$seqnum++;
}
close $dev;

