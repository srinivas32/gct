use IO::File;
use Getopt::Long;

my $perl_module = 0;
my $c_header = 0;

GetOptions("-perl" => \$perl_module,
           "-header" => \$c_header);

my $types = &read_input();

if($c_header)
{
    &write_c_header($types);
}
if($perl_module)
{
    &write_perl_modules($types);
}

# Write a perl module for each enumerated type defined in the $type hash
sub write_perl_modules
{
    my $types = shift;
    my $module_name;
    my $module;
    my $basename;

    foreach my $type (sort keys %{$types})
    {
	$module_name = $type;
	$module_name =~ s/globus_gram_protocol_//;
	$module_name =~ s/_t$//;
	$module_name = join("", map {ucfirst} (split(/_/,$module_name)));

	$basename = $type;
	$basename =~ s/_t$//;
	$basename = uc $basename;

	$module = new IO::File(">$module_name.pm");
	$module->print(<<EOF);
package Globus::GRAM::$module_name;

=head1 NAME

Globus::GRAM::$module_name - GRAM Protocol $module_name Constants

=head1 DESCRIPTION

The Globus::GRAM::$module_name module defines symbolic names for the
$module_name constants in the GRAM Protocol.

EOF

	# Errors are typed; other enumerations are just constant values
	if($module_name eq 'Error')
	{
	    $module->print(<<EOF);
=pod

The Globus::GRAM::Error module methods return an object consisting
of an integer erorr code, and (optionally) a string explaining the
error. 

=head2 Methods

=over 4

=item \$error = new Globus::GRAM::Error(\$number, \$string);

Create a new error object with the given error number and string
description. This is called by the error-specific factory methods described
below.

=cut

sub new
{
    my \$proto = shift;
    my \$class = ref(\$proto) || \$proto;
    my \$self = {};
    my \$value = shift;
    my \$string = shift;

    \$self->{value} = \$value if defined(\$value);
    \$self->{string} = \$string if defined(\$string);

    bless \$self, \$class;

    return \$self;
}

=item \$error->string()

Return the error string associated with a Globus::GRAM::Error object.

=cut

sub string
{
    my \$self = shift;
    return \$self->{string};
}

=item \$error->value()

Return the integer error code associated with a Globus::GRAM::Error object.

=cut

sub value
{
    my \$self = shift;
    return \$self->{value};
}
EOF
	}
	else
	{
	    $module->print(<<EOF);

=head2 Methods

=over 4

EOF
	}
	foreach (@{$types->{$type}->{enumeration}})
	{
	    $scopedname = $_->{name};
	    $scopedname =~ s/${basename}_//;

	    if($module_name eq 'Error')
	    {
		$module->print(<<EOF);

=item \$error = Globus::GRAM::Error::$scopedname()

Create a new $scopedname GRAM error.

=cut

sub $scopedname
{
    return new Globus::GRAM::Error($_->{value});
}
EOF
	    }
	    else
	    {
		$module->print(<<EOF);

=item \$value = Globus::GRAM::$module_name::$scopedname()

Return the value of the $scopedname constant.

=cut

sub $scopedname
{
    return $_->{value};
}
EOF
	    }
	}

	$module->print(<<EOF);

=back

=cut

1;
EOF
	$module->close();
    }
}

sub write_c_header
{
    my $types = shift;
    my $output = new IO::File(">globus_gram_protocol_constants.h");
    my $basename;

    $output->print(<<EOF);
/* This file is automatically generated by
 * $0. Do not modify.
 */

/**
 * \@file globus_gram_protocol_constants.h Protocol Constants
 */
#ifndef GLOBUS_GRAM_PROTOCOL_CONSTANTS_H
#define GLOBUS_GRAM_PROTOCOL_CONSTANTS_H

EOF
    foreach my $type (sort keys %{$types})
    {
	$break = "\n";
	$basename = $type;
	$basename =~ s/_t//;

	$output->print(<<EOF);
/**
 * \@defgroup $basename $types->{$type}->{brief}
 * \@ingroup globus_gram_protocol
 * \@brief $types->{$type}->{brief}
 * \@details
 * $types->{$type}->{documentation}
 * \@anchor $type
 */

/**
 * $types->{$type}->{brief}
 * \@ingroup $basename
 */
typedef enum
{
EOF
	foreach (@{$types->{$type}->{enumeration}})
	{
	    $output->print("$break    "
	                   . $_->{name} . "=" . $_->{value} 
		           . "/**< $_->{doc} */");
	    $break = ",\n";
	}
	$output->print("\n}\n$type;\n\n");
    }
    $output->print("#endif\n");
    $output->close();
}

sub read_input
{
    my $types = {};
    my $type_name = "";
    my $documentation = "";
    my $enumeration = undef;

    while(<>)
    {
	next if(/^#[^#]/);

	if($_ eq "\n")
	{
	    # end of type
	    &insert_type($types, $type_name, $brief_documentation, $documentation, $enumeration);

	    $type_name = "";
	    $brief_documentation = "";
	    $documentation = "";
	    $enumeration = undef;
	}
	elsif(m/^##[^#]/)
	{
	    # type name and/or documentation
	    #
	    chomp;
	    s/^##\s*//;

	    if($type_name eq "")
	    {
		($type_name, $brief_documentation) = split(/\s+/, $_, 2);
	    }
	    else
	    {
		$documentation .= " $_";
	    }
	}
	elsif(m/^###/)
	{
	    s/^###\s+//;

	    $enumerated_doc .= $_;
	}
	else
	{
	    chomp;
	    ($enumerated_name, $enumerated_value) = split(/=/, $_, 2);

	    push(@{$enumeration},
	         {
		     name => $enumerated_name,
		     value => $enumerated_value,
		     doc => $enumerated_doc
		 });
	    $enumerated_name = $enumerated_value = $enumerated_doc = "";
	}
    }
    &insert_type($types, $type_name, $brief_documentation, $documentation, $enumeration);
    return $types;
}

sub insert_type
{
    my $types = shift;
    my $type_name = shift;
    my $brief = shift;
    my $documentation = shift;
    my $enumeration = shift;

    if($type_name eq "")
    {
	return $types;
    }

    $types->{$type_name}{brief} = $brief;
    $types->{$type_name}{documentation} = $documentation;
    $types->{$type_name}{enumeration} = $enumeration;

    return $types;
}
