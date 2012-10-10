#!/usr/bin/perl -w

# do some simple statistics on a raw Mode-S "AVR" log (with timestamps)

my $dbfn_aircraft = "../data/aircraft.txt";
my $dbfn_airlines = "../data/airlines.txt";

use warnings;
use strict;
use Time::HiRes qw(gettimeofday);

sub readCSV {
	my $fn = shift;
	my $sep = shift || ',';
	my @out;
	open(F, "<$fn") || die "unable to open $fn";
	my @head = ();
	while (<F>) {
		chomp;
		next if /^\s*#/;
		my @line = split(/$sep/);
		if (@head == 0) {
			@head = @line;
			next;
		}

		my %row;
		for (my $i = 0; $i <= $#line; $i++) {
			last if not defined $head[$i];
			$row{$head[$i]} = $line[$i];
		}
		push @out, \%row;
	}
	close F;
	return \@out;
}

sub getCRC56 {
	my @pkt = @{shift @_};

	my $crc = ($pkt[0] << 24) | ($pkt[1] << 16) | ($pkt[2] << 8) | $pkt[3];
	foreach (1..32) {
		$crc ^= 0xfffa0480 if ($crc & 0x80000000);
		$crc <<= 1;
	}
	$crc >>= 8;

	return $crc;
}

sub getCRC112 {
	my @pkt = @{shift @_};

	my $zero = ($pkt[0] << 24) | ($pkt[1] << 16) | ($pkt[2] << 8) | $pkt[3];
	my $one  = ($pkt[4] << 24) | ($pkt[5] << 16) | ($pkt[6] << 8) | $pkt[7];
	my $two  = ($pkt[8] << 24) | ($pkt[9] << 16) | ($pkt[10] << 8);
	foreach (1..88) {
		$zero ^= 0xfffa0480 if ($zero & 0x80000000);
		$zero <<= 1;
		$zero |= 1 if ($one & 0x80000000);
		$one <<= 1;
		$one |= 1 if ($two & 0x80000000);
		$two <<= 1;
	
	}
	$zero >>= 8;

	return $zero;
}

sub fsToStr {
	my $fs = shift;
	my @fsvals = (  "airborne", "ground", "alert,airborne", "alert,ground",
			"alert,SPI", "SPI", "reserved", "unassigned");
	return $fsvals[$fs & 0x7];
}

# XXX fill in the rest, clean up.
sub identchr {
	my $six = shift;
	return (chr(ord('A') + $six - 1)) if ($six >= 1 && $six <= 26);
	return ' ' if ($six == 0x20);
	return chr($six) if ($six >= 0x30 && $six <= 0x39);
	return '?';
}

sub acToStr {
	my ($pkt) = @_;
	my $alt = undef;

	# 3.1.2.6.5.4
	my $m = ($pkt->[3] >> 6) & 1; # bit 26
	if ($m == 0) {
		my $q = ($pkt->[3] >> 4) & 1; # bit 28 (if M=0)

		if ($q) {
			# 11 bits big-endian, but they're not contiguous!
			$alt = (($pkt->[2] & 0x1f) << 6) | ((($pkt->[3] >> 7) & 0x1) << 5) | ((($pkt->[3] >> 5) & 0x1) << 4) | ($pkt->[3] & 0xf);
			$alt *= 25; $alt -= 1000;
		} else { # Mode C grey code
			# XXX
			$alt = "grey";
		}
	}
	return $alt;
}

sub vsToStr {
	my ($pkt) = @_;
	return ($pkt->[0] & 0x04) ? "ground" : "airborne";
}

# XXX what else is in RI? 7 bits leftover. =2 is frequent.
sub riToStr {
	my ($pkt) = @_;
	my $ri = (($pkt->[1] & 0x7) << 1) | (($pkt->[2] & 0x80) >> 7);
	return "unsupported" if ($ri == 0);
	return "undefined" if ($ri == 1);
	if ($ri == 9) { return "<75"; }
	elsif ($ri == 10) { return "75-150"; }
	elsif ($ri == 11) { return "150-300"; }
	elsif ($ri == 12) { return "300-600"; }
	elsif ($ri == 13) { return "600-1200"; }
	elsif ($ri == 14) { return ">1200"; }
	return "[$ri]";
}

sub decodeModeS {
	my $pktStr = shift;
	my $ext;
	if ($pktStr =~ /^[A-F0-9]{14}$/) { $ext = 0; }
	elsif ($pktStr =~ /^[A-F0-9]{28}$/) { $ext = 1; }
	else { return; }

	my %attrs;
	my @pkt = map { hex($_); } ($pktStr =~ /(\w{2})/gm);

	$attrs{'df'} = ($pkt[0] >> 3) & 0x1f;

	if ($ext) { # 112 bit
		$attrs{'ap'} = (($pkt[11] << 16) | ($pkt[12] << 8) | $pkt[13]) & 0xffffff;
		$attrs{'crc'} = getCRC112(\@pkt);
	} else { # 56 bit
		$attrs{'ap'} = (($pkt[4] << 16) | ($pkt[5] << 8) | $pkt[6]) & 0xffffff;
		$attrs{'crc'} = getCRC56(\@pkt);
	}

	my $aa;
	if ($attrs{'df'} == 11 || $attrs{'df'} == 17 || $attrs{'df'} == 18) {
		$aa = ($pkt[1] << 16) | ($pkt[2] << 8) | $pkt[3]; #XXX er, no, wrong.
	} else {
		$aa = $attrs{'ap'} ^ $attrs{'crc'};
	}
	$attrs{'aa'} = $aa;

	if ($attrs{'df'} ==  0) {

		$attrs{'pressure_altitude'} = acToStr(\@pkt);
		$attrs{'vertical_status'} = vsToStr(\@pkt);
		$attrs{'max_airspeed'} = riToStr(\@pkt);

	} elsif ($attrs{'df'} ==  4) {

		$attrs{'pressure_altitude'} = acToStr(\@pkt);
		$attrs{'fs'} = fsToStr($pkt[0] & 7);

	} elsif ($attrs{'df'} == 17) {

		$attrs{'ca'} = $pkt[0] & 0x7;

		my $type = ($pkt[4] >> 3) & 0x1f;
		my $subtype = $pkt[4] & 0x7;
		$attrs{'me_type'} = "$type.$subtype";

		if ($type >= 1 && $type <= 4) {
			my $ident = "";
			# XXX make this a sub that runs twice.
			$ident .= identchr( ($pkt[5] >> 2) & 0x3f );
			$ident .= identchr( (($pkt[5] & 0x3) << 4) | (($pkt[6] >> 4) & 0xf) );
			$ident .= identchr( (($pkt[6] & 0xf) << 2) | (($pkt[7] >> 6) & 0x3) );
			$ident .= identchr( $pkt[7] & 0x3f );
			$ident .= identchr( ($pkt[8] >> 2) & 0x3f );
			$ident .= identchr( (($pkt[8] & 0x3) << 4) | (($pkt[9] >> 4) & 0xf) );
			$ident .= identchr( (($pkt[9] & 0xf) << 2) | (($pkt[10] >> 6) & 0x3) );
			$ident .= identchr( $pkt[10] & 0x3f );

			$attrs{'ident'} = $ident;
		}

	} elsif ($attrs{'df'} == 20) {

		$attrs{'pressure_altitude'} = acToStr(\@pkt);

		$attrs{'fs'} = fsToStr($pkt[0] & 7);

		my $mb;
		for my $i (4..10) { $mb .= sprintf("%02x", $pkt[$i]); }
		$attrs{'mb'} = $mb;
	}

	return \%attrs;
}

my %squits = ( 'total' => 0, 'ignoredlines' => 0, 'standard' => 0, 'extended' => 0, 'first' => undef, 'last' => undef, 'df' => {} );
my %aircraft;
my $db_aircraft_list = readCSV($dbfn_aircraft, '\t');
my %db_aircraft;
foreach my $ac (@{$db_aircraft_list}) {
	next unless defined $ac->{'ICAO24'};
	# these will always be in uppercase btw
	$db_aircraft{$ac->{'ICAO24'}} = $ac;
}

my $db_airlines_list = readCSV($dbfn_airlines, '\t');
my %db_airlines;
foreach my $al (@{$db_airlines_list}) {
	next unless defined $al->{'ICAO'};
	$db_airlines{$al->{'ICAO'}} = $al;
}

while (my $line = <>) {
	$line =~ s/\r//g;
	chomp($line);
	my ($rxtime, $modeS);
	if (($line =~ /^(([0-9]+)(?:\.([0-9]+)|)\s+|)\*(.*)\;$/)) {
        ($rxtime, $modeS) = (($1 ne "") ? $1 : undef, $4);
	} elsif (($line =~ /^\@(.{12})(.{28}|.{56});$/)) {
        ($rxtime, $modeS) = (undef, $2);
    } else {
		$squits{'ignoredlines'}++;
		next;
	}

	my $attrs = decodeModeS($modeS);

	if ($rxtime) {
		$squits{'first'} = $rxtime if (!defined $squits{'first'} or ($rxtime < $squits{'first'}));
		$squits{'last'} = $rxtime if (!defined $squits{'last'} or ($rxtime > $squits{'last'}));
	}
	$squits{'total'}++;
	$squits{(length $modeS == 14) ? 'standard' : 'extended'}++;

	if (defined $attrs->{'df'}) {
		$squits{'df'}->{$attrs->{'df'}}++;
	} else {
		$squits{'df'}->{'-1'}++;
	}

	if (defined $attrs->{'aa'}) {
		my $aa = $attrs->{'aa'};
		$aircraft{$aa}->{'squitters'}->{'total'}++;
		$aircraft{$aa}->{'squitters'}->{(length $modeS == 14) ? 'standard' : 'extended'}++;
		if (defined $attrs->{'df'}) {
			$aircraft{$aa}->{'squitters'}->{'df'}->{$attrs->{'df'}}++;
		} else {
			$aircraft{$aa}->{'squitters'}->{'df'}->{'-1'}++;
		}
			
	} else {
		$aircraft{hex('ffffff')}->{'squitters'}->{'total'}++;
	}
}

my %acsummary = ('total' => 0, 'extended' => 0, 'adsb' => 0);
foreach my $ac (keys %aircraft) {
	$acsummary{'total'}++;
	$acsummary{'extended'}++ if (($aircraft{$ac}->{'squitters'}->{'extended'} || 0) > 0);
	$acsummary{'adsb'}++ if (($aircraft{$ac}->{'squitters'}->{'df'}->{'17'} || 0) > 0);
}

print "\n";
print "Squitter summary:\n";
print "\tIgnored input lines: " . $squits{'ignoredlines'} . "\n" if ($squits{'ignoredlines'} > 0);
print "\tTotal: " . $squits{'total'} . "\n";
print "\tStandard: " . $squits{'standard'} . " (" . sprintf("%2.02f", ($squits{'standard'}/$squits{'total'}*100)) . "%)\n";
print "\tExtended: " . $squits{'extended'} . " (" . sprintf("%2.02f", ($squits{'extended'}/$squits{'total'}*100)) . "%)\n";
if ($squits{'first'} && $squits{'last'}) {
	print "\tTime range: " . $squits{'first'} . " to " . $squits{'last'} . "\n";
}
print "\tDF:\n";
foreach my $df (sort { $a <=> $b } keys %{$squits{'df'}}) {
	print "\t\t$df\t" . $squits{'df'}->{$df} . " (" . sprintf("%2.02f", ($squits{'df'}->{$df}/$squits{'total'}*100)) . "%)\n";
}
print "\n";

print "Aircraft summary:\n";
print "\tTotal: " . $acsummary{'total'} . "\n";
print "\tSupport 1090ES: " . $acsummary{'extended'} . " (" . sprintf("%2.02f", ($acsummary{'extended'}/$acsummary{'total'})*100) . "%)\n";
print "\tSupport ADS-B: " . $acsummary{'adsb'} . " (" . sprintf("%2.02f", ($acsummary{'adsb'}/$acsummary{'total'})*100) . "%)\n";
print "\n";

if (1) {
	print "Airframes:\n";
	foreach my $ac (reverse sort { $aircraft{$a}->{'squitters'}->{'total'} <=> $aircraft{$b}->{'squitters'}->{'total'} } keys %aircraft) {
		my $icao = uc(sprintf("%06x", $ac));
		print "\t" . sprintf("%15d", $aircraft{$ac}->{'squitters'}->{'total'}) . "\t$icao";
		if (($aircraft{$ac}->{'squitters'}->{'df'}->{'17'} || 0) > 0) {
			print "\t ADS-B";
		} elsif (($aircraft{$ac}->{'squitters'}->{'extended'} || 0) > 0) {
			print "\t1090ES";
		} else {
			print "\tMode-S";
		}
		if (defined $db_aircraft{$icao}) {
			printf("\t%-10s\t%-5s\t%-4s\t%-3s",
				$db_aircraft{$icao}->{'Reg'} || "", 
				$db_aircraft{$icao}->{'ICAOType'} || "", 
				$db_aircraft{$icao}->{'CYear'} || "", 
				$db_aircraft{$icao}->{'Registrant'} || "");
			if (defined $db_aircraft{$icao}->{'Registrant'}) {
				my $opb = $db_aircraft{$icao}->{'Registrant'};
				if ( ($opb =~ s/^([A-Z]+)\s*.*?$/$1/) && (defined $db_airlines{$opb}) ) {
					printf("\t%-20s", $db_airlines{$opb}->{'Callsign'});
				}
			}
		}
		print "\n";
	}
}

