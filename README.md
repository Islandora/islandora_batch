BUILD STATUS
------------
Current build status:
[![Build Status](https://travis-ci.org/Islandora/islandora_batch.png?branch=7.x)](https://travis-ci.org/Islandora/islandora_batch)

CI Server:
http://jenkins.discoverygarden.ca


ISLANDORA BATCH
==================

CONTENTS OF THIS FILE
---------------------

 * summary
 * requirements
 * installation
 * configuration
 * usage
 * customization
 * troubleshooting
 * faq
 * contact
 * sponsors


SUMMARY
-------

This module implements a batch framework, as well as a basic ZIP/directory
ingester.

The ingest is a two-step process:
* Preprocessing: The data is scanned, and a number of entries created in the
  Drupal database.  There is minimal processing done at this point, so it can
  complete outside of a batch process.
* Ingest: The data is actually processed and ingested. This happens inside of
  a Drupal batch.

REQUIREMENTS
------------

A full Islandora installation.

INSTALLATION
------------

Install in the same way as any other Drupal module.

CONFIGURATION
-------------

USAGE
-------------

There base ZIP/directory preprocessor can be called as a drush script, so
something like (see "drush help islandora_batch_scan_preprocess" for
additional parameters):
"drush -v --user=admin --uri=http://localhost islandora_batch_scan_preprocess --type=zip --target=/path/to/archive.zip"
should serve to populate the queue (stored in the Drupal database) with base entries.

For the base scan, files are grouped according to their basename (without extension);
DC, MODS or MARCXML in a *.xml or binary MARC in a *.mrc will be transformed to
both MODS and DC, and the first entry with another extension will be used to
create an "OBJ" datastream. Where there is a basename with no matching .xml
or .mrc, some XML will be created which simply uses the filename as the title.

The queue of preprocessed items can then be processed something like:
"drush -v --user=admin --uri=http://localhost islandora_batch_ingest"

CUSTOMIZATION
-------------

Custom ingests can be written by extending any of the existing preprocessors
and batch object implementations.

TROUBLESHOOTING
---------------


F.A.Q.
------


CONTACT
-------


SPONSORS
--------

