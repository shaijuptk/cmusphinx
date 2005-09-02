#!/usr/bin/perl
# ====================================================================
# Copyright (c) 2000 Carnegie Mellon University.  All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer. 
#
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in
#    the documentation and/or other materials provided with the
#    distribution.
#
# This work was supported in part by funding from the Defense Advanced 
# Research Projects Agency and the National Science Foundation of the 
# United States of America, and the CMU Sphinx Speech Consortium.
#
# THIS SOFTWARE IS PROVIDED BY CARNEGIE MELLON UNIVERSITY ``AS IS'' AND 
# ANY EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, 
# THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL CARNEGIE MELLON UNIVERSITY
# NOR ITS EMPLOYEES BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, 
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY 
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# ====================================================================
#
#  Set up script
#
# ====================================================================

use strict;
use File::Copy;
use Cwd;
use Getopt::Long;
use Pod::Usage;
use File::stat;

if ($#ARGV == -1) {
  pod2usage(2);
}

my ($SPHINX_TRAIN_CFG,
    $SPHINX_DECODER_DIR,
    $DBNAME,
    $help,
    $force,
    $update);

my $LEAVE_MODE = 0;
my $UPDATE_MODE = 1;
my $FORCE_MODE = 2;

my $replace_mode = $LEAVE_MODE;

$SPHINX_DECODER_DIR = $0;

$SPHINX_DECODER_DIR =~ s/^(.*)[\/\\]scripts[\\\/].*$/$1/i;

# Default location for sphinx_train.cfg
$SPHINX_TRAIN_CFG = "./etc/sphinx_train.cfg";

# Set some defaults
my $language_model;
my $language_weight = 10;
my $beam_width = 1e-80;
my $word_beam = 1e-40;

# Default align code
my $align;

my $result = GetOptions('help|h' => \$help,
			'force' => \$force,
			'update' => \$update,
			'sphinxtraincfg|stcfg=s' => \$SPHINX_TRAIN_CFG,
			'sphinx3dir|s3=s' => \$SPHINX_DECODER_DIR,
			'task=s' => \$DBNAME,
			'langmod=s' => \$language_model,
			'langwt=f' => \$language_weight,
			'bw=f' => \$beam_width,
			'wbeam=f' => \$word_beam,
			'align=s' => \$align);

if (($result == 0) or (defined($help)) or (!defined($DBNAME)) or (!defined $language_model)) {
  pod2usage( -verbose => 1 );
  exit(-1);
}

$replace_mode = $UPDATE_MODE if (defined $update);
$replace_mode = $FORCE_MODE if (defined $force);

# Check if the current directory - where the setup will be installed - is empty
opendir(DIR, ".") or die "Can't open current directory";
my @dirlist = grep !/^\./, readdir DIR;
closedir(DIR);

if ($#dirlist > 0) {
  print "Current directory not empty.\n";
  if ($replace_mode == $FORCE_MODE) {
    print "Will overwrite existing files.\n";
  } elsif ($replace_mode == $UPDATE_MODE) {
    print "Will overwrite existing files if they're older.\n";
  } else {
    print "Will leave existing files as they are, and copy non-existing files.\n";
  }
}

# Start building the directory structure
print "Making basic directory structure.\n";
mkdir "bin" unless -d bin;
mkdir "etc" unless -d etc;
mkdir "feat" unless -e feat;
mkdir "wav" unless -e wav;

mkdir "logdir" unless -d logdir;

# Figure out the platform string definition
my $PLATFORM = &machine_type();
if ($PLATFORM eq "unknown") {
  die "Don't know the current platform.\n";
}
my $INSTALL = "$SPHINX_DECODER_DIR";
print "Platform: $PLATFORM\n";
print "Install: $INSTALL\n";

# Copy all executables to the local bin directory. 
my $execdir = "$SPHINX_DECODER_DIR/bin/$PLATFORM";
opendir(DIR, "$execdir") or die "Can't open $INSTALL/bin";

print "Copying executables from $execdir\n";
@dirlist = grep !/^\./, readdir DIR;
closedir(DIR);
foreach my $executable (@dirlist) {
 replace_file("$execdir/$executable",
	      "bin/$executable",
	      $replace_mode) if (-f "$execdir/$executable");
}

# We copy the scripts from the scripts directory directly.
mkdir "scripts_pl" unless -d scripts_pl;
my $SCRIPTDIR = "scripts_pl/decode";
mkdir "$SCRIPTDIR" unless -d $SCRIPTDIR;
my $script_in_dir = "$SPHINX_DECODER_DIR/scripts";
print "Copying scripts from the scripts directory\n";
@dirlist = ();
# Currently, only the top level scripts directory
push @dirlist, ".";

# Copy the directory tree. We do so by creating each directory, and
# then copying it to the correct location here. We also set the permissions.
foreach my $directory (@dirlist) {
  mkdir "$SCRIPTDIR/$directory" unless -d "$SCRIPTDIR/$directory";
  opendir(SUBDIR, "$script_in_dir/$directory") or 
    die "Can't open subdir $directory\n";
  my @subdirlist = grep /\.pl$/, readdir SUBDIR;
  closedir(SUBDIR);
  foreach my $executable (@subdirlist) {
    replace_file("$script_in_dir/$directory/$executable",
		 "$SCRIPTDIR/$directory/$executable",
		 $replace_mode);
    chmod 0755, "$SCRIPTDIR/$directory/$executable";
  }
}

# Set the permissions to executable;
opendir(DIR, "bin") or die "Can't open bin directory\n";
@dirlist = grep !/^\./, readdir DIR;
closedir(DIR);
@dirlist = map { "bin/$_" } @dirlist;
chmod 0755, @dirlist;

# Finally, we generate the config file for this specific task
print "Generating s3flat specific scripts and config file\n";
open (CFGIN, "$script_in_dir/s3flat.cfg") or 
  die "Can't open $script_in_dir/s3flat.cfg\n";
open (CFGOUT, ">etc/sphinx_decode.cfg") or die "Can't open etc/sphinx_decode.cfg\n";

$align = 'builtin' unless (-e $align);

while (<CFGIN>) {
  chomp;
  s/___DB_NAME___/$DBNAME/g;
  my $currDir = cwd;
  s/___BASE_DIR___/$currDir/g;
  s/___SPHINXTRAIN_CFG___/$SPHINX_TRAIN_CFG/g;
  s/___SPHINXDECODER_DIR___/$SPHINX_DECODER_DIR/g;
  s/___LANGMODEL___/$language_model/g;
  s/___LANGWEIGHT___/$language_weight/g;
  s/___BEAMWIDTH___/$beam_width/g;
  s/___WORDBEAM___/$word_beam/g;
  s/___ALIGN___/$align/g;
  print CFGOUT "$_\n";
}
close(CFGIN);
close(CFGOUT);

print "Set up for decoding $DBNAME using Sphinx-3 complete\n";

sub replace_file {
  my $source = shift;
  my $destination = shift;
  my $replace_mode = shift;

  if (($replace_mode == $FORCE_MODE) or (! -s $destination)) {
#    print "Replacing file $destination with $source\n";
    copy("$source", "$destination");
  } elsif ($replace_mode == $UPDATE_MODE) {
    my $source_time = stat($source);
    my $dest_time = stat($destination);
    if (($source_time->mtime) > ($dest_time->mtime)) {
      copy("$source", "$destination");
    }
  }
}

sub machine_type {
  my $type = lc(`uname 2>&1`);
  chomp($type);
  my $mach_type = "unknown";
  if ($type eq "osf1") {
    # DEC Alpha osf1
    $mach_type = "alpha";
  } elsif ($type eq "hp") {
    # HP UX
    $mach_type = "hp";
  } elsif ($type eq "sunos") {
    # SunOS, Solaris etc
    $mach_type = "sun4";
  } elsif ($type eq "linux") {
    $mach_type = "linux";
  }
  return $mach_type;
}

__END__

=head1 NAME

setup_s3flat.pl - setup the sphinx3flat environment for a new task

=head1 SYNOPSIS

=over 4

=item To setup a new sphinx3flat task

Create the new directory (e.g., mkdir RM1)

Go to the new directory (e.g., cd RM1)

Run this script (e.g., perl ../sphinx3/scripts/setup_s3flat.pl RM1)

=item ./scripts/setup_s3flat.pl -help

For full list of arguments

=item  ./scripts/setup_s3flat.pl [-force] [-sphinxtraincfg <SphinxTrain config file>] [-sphinx3dir <sphinx3flat directory>] -task <task name>

For setting up the sphinx3flat environment, located at <sphinx3flat directory>, into current directory, naming the task <task name>

=back

=head1 ARGUMENTS

=over 4

=item B<-force>

Force the setup script to overwrite existing files. Optional.

=item B<-update>

Update existing files if they are older than in sphinx3. Optional.

=item B<-langmod>

Language model for this task, provided by user.

=item B<-langwt>

Language weight for this task, default 10.

=item B<-bw>

Beam width used by the decoder, default 1e-80.

=item B<-wbeam>

Word beam width, default 1e-40.

=item B<-align>

Path to an executable used for aligning reference text and decoder hypothesis. Optional.

=item B<-sphinxtraincfg>

The location of the SphinxTrain configuration file. If not provided, the default "etc/sphinx_train.cfg" is assumed. Optional.

=item B<-sphinx3dir>

The location of the sphinx3flat package. If not provided, same location as this script is assumed. Optional.

=item B<-task>

The name of the new task. Required.

=item B<-help>

The help screen (this screen). Optional.

=back

=cut
