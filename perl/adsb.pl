#!/usr/bin/perl -w

# barebones squitter decoder

use warnings;
use strict;
use Time::HiRes qw(gettimeofday);

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

sub updateAircraftInfo {
	my $achash = shift;
	my $ac = shift;
}

my %aircraft;

# *02C1873C4A7C91;
# *5DABA5F447B20E;
# *20000733F678BA;
# *5DA2F71F5E533B;
# *028194A8304BFD;
while (my $line = <>) {
	$line =~ s/\r//g;
	chomp($line);
	next unless ($line =~ /^([0-9\.]+\s+|)\*(.*)\;$/);
	my ($rxtime, $modeS) = ($1, $2);

	$rxtime ||= (gettimeofday + 0);
	my $attrs = decodeModeS(uc $modeS);

	print "$line\t\t";
	print "\t" if (length($modeS) < 28);
	foreach my $k (sort keys %{$attrs}) {
		print "$k=";
		if ($k =~ /crc|aa|ap/) {
			print "" . sprintf("%06x", ($attrs->{$k} || 0));
		} else {
			print "" . ($attrs->{$k} || "");
		}
		print "\t";
	}

	print "\n";

	updateAircraftInfo(\%aircraft, $attrs, $rxtime);

}


