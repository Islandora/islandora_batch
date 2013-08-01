<?php

/**
 * @file
 * A basic batch ingest implementation (preprocessor and object).
 *
 * Works much the same as the zip importer in the islandora_importer module,
 * or the old D6 zip batch ingester.
 */

// @codingStandardsIgnoreStart
// This Whole file is ignored due to classes and methods are not camelcase and it
// being called all over the place.  All of the other coding standards are met.

/**
 * A class which is capable of getting the listing of a directory or zip file.
 *
 * Used as a basis for the book batch. Really, just adds a couple methods to
 * help out.
 */
class IslandoraScanBatch extends IslandoraBatchPreprocessor {
  /**
   * Get a listing of "file-object"-like entries.
   *
   * @return array
   *   An associative array of stdClass objects representing files. Array keys
   *   are URIs relative to the "target", and the objects properties include:
   *   - uri: A string containing the complete URI of the resource.
   *   - filename: The filename.
   *   - name: The filename without its extension.
   */
  protected function scan() {
    $method = "scan_" . $this->get_target_type();
    return $this->$method($this->get_target());
  }

  /**
   * Get the target resource.
   */
  protected function get_target() {
    return $this->parameters['target'];
  }

  /**
   * Get the type of the target resource.
   *
   * Prefixed with "scan_" to determine which method gets called to generate
   * the list of resource.
   */
  protected function get_target_type() {
    return $this->parameters['type'];
  }

  /**
   * Allow the pattern to be set differently.
   */
  protected static function get_pattern() {
    return '/.*/';
  }

  /**
   * Scan the directory with file_scan_directory().
   */
  protected function scan_directory($target) {
    $files = array();
    $target_components = explode(DIRECTORY_SEPARATOR, $target);
    $target = implode(DIRECTORY_SEPARATOR, $target_components);
    $target_length = strlen($target) + 1;
    foreach (file_scan_directory($target, $this->get_pattern()) as $uri => $value) {
      $files[substr($uri, $target_length)] = $value;
    }

    return $files;
  }

  /**
   * Generate output analogous to file_scan_directory().
   */
  protected function scan_zip($target) {
    $zip = new ZipArchive();

    $zip_file = drupal_realpath($target);
    if (($error = $zip->open($zip_file)) !== TRUE) {
      return FALSE;
    }

    $count = $zip->numFiles;

    $info = array();
    for ($i = 0; $i < $count; $i++) {
      $name = $zip->getNameIndex($i);
      $path_info = pathinfo($name);

      // Avoid creating entries for directories and some weird Mac stuff...
      // XXX: Should be made to take the pattern from
      // $this::get_pattern(), and to filter the results based on it.
      if (isset($path_info['extension']) && strpos($path_info['dirname'], '__MACOSX') === FALSE && $path_info['extension'] != 'DS_Store') {
        $file_entry = new stdClass();
        $file_entry->uri = "zip://$zip_file#$name";
        $file_entry->filename = $path_info['basename'];
        $file_entry->name = $path_info['filename'];
        $info[$name] = $file_entry;
      }
    }

    $zip->close();

    return $info;
  }

  /**
   * Group file entries logically, to pass off to the import object.
   *
   * Default implementation groups on files' basename.
   *
   * @param array $files
   *   An array, as returned by file_scan_directory().
   */
  protected function group_files($files) {
    $grouped = array();

    // TODO: Group according to basename, to and hand-off each group to object
    // wrappers.
    foreach ($files as $uri => &$file_object) {
      $file_object->ext = pathinfo($file_object->filename, PATHINFO_EXTENSION);
      $grouped[$file_object->name][$file_object->ext] = $file_object;
    }

    return $grouped;
  }

  /**
   * Get the name of the class to instantiate for the batch operations.
   */
  protected static function get_object_class() {
    return "IslandoraScanBatchObject";
  }

  /**
   * Perform preprocessing of the scanned resources.
   */
  public function preprocess() {
    $files = $this->scan();

    if ($files === FALSE) {
      // Failed to scan.
      // TODO: Pop an error.
    }
    elseif (empty($files)) {
      // No contents.
      // TODO: Pop a warning.
    }

    $added = array();

    $grouped = $this->group_files($files);

    $object_class = static::get_object_class();
    foreach ($grouped as $base_name => $object_info) {
      $ingest_object = new $object_class($this->connection, $base_name, $object_info, $this->parameters);

      // XXX: Might be better to have this actually handled as another
      // "preprocessor", so arbitrary "tree" structures might be built?
      $added = array_merge($added, $this->preprocess_children($ingest_object));
    }

    return $added;
  }

  /**
   * Recursively attempt to preprocess children.
   */
  protected function preprocess_children(IslandoraBatchObject $object, $parent = NULL) {
    $to_return = array();

    // XXX: Squash exceptions and log 'em.
    try {
      $object->add_relationships();
      $this->add_to_database($object, $object->get_resources(), $parent);
      $to_return[] = $object;

      foreach ($object->get_children($this->connection) as $child) {
        $to_return = array_merge($to_return, $this->preprocess_children($child, $object->id));
      }
    }
    catch (Exception $e) {
      watchdog_exception('islandora_batch', $e);
    }

    return $to_return;
  }
}

class IslandoraScanBatchObject extends IslandoraBatchObject {
  protected $base_name, $object_info, $preprocessor_parameters;

  /**
   * Constructor for the IslandoraScanBatchObject.
   */
  public function __construct(IslandoraTuque $connection, $base_name, $object_info, $preprocessor_parameters) {
    parent::__construct(NULL, $connection->repository);

    $this->base_name = $base_name;
    $this->object_info = $object_info;
    $this->resources = array();
    $this->preprocessor_parameters = $preprocessor_parameters;
  }

  /**
   * Static Function to get mime.
   */
  protected static function get_mime($file_object) {
    static $mime_detect = NULL;
    if ($mime_detect === NULL) {
      $mime_detect = new MimeDetect();
    }

    return $mime_detect->getMimetype($file_object->filename);
  }

  /**
   * Function batch_process.
   */
  public function batch_process() {
    // Use object_info to create some datastreams.
    $this->label = $this->get_title();
    $this->get_mods();
    $this->get_dc();

    if (!isset($this['OBJ'])) {
      $meta = array_map(NULL, array('xml', 'mrc'));
      $other = array_diff_key($this->object_info, $meta);

      if ($obj = reset($other)) {
        $obj_datastream = $this->constructDatastream('OBJ');
        // Figure out the mimetype.
        $obj_datastream->mimetype = static::get_mime($obj);
        $obj_datastream->label = 'OBJ Datastream';
        $obj_datastream->setContentFromFile($obj->uri, FALSE);

        $this->ingestDatastream($obj_datastream);
      }
    }

    return ISLANDORA_BATCH_STATE__DONE;
  }

  /**
   * Get resources for the current object.
   */
  public function get_resources() {
    return $this->resources;
  }

  /**
   * Get the title for this object.
   *
   * Gets the label not empty; otherwise, grabs a title from the MODS.
   */
  protected function get_title() {
    if (!isset($this->label) || empty($this->label)) {
      // Acquire a title.
      $mods = $this->get_mods();
      if ($mods) {
        $mods_doc = new DOMDocument();
        $mods_doc->loadXML($mods);
        $mods_xpath = new DOMXPath($mods_doc);
        $mods_xpath->registerNamespace('m', 'http://www.loc.gov/mods/v3');

        $this->label = $mods_xpath->evaluate('string(//m:mods/m:titleInfo/m:title/text())');
      }
    }

    return $this->label;
  }

  /**
   * Run an XSLT, and return the results.
   *
   * @param array $info
   *   An associative array of parameters, containing:
   *   - input: The input XML in a string.
   *   - xsl: The path to an XSLT file.
   *   - php_functions: Either a string containing one or an array containing
   *     any number of functions to register with the XSLT processor.
   *
   * @return string
   *   The transformed XML, as a string.
   */
  protected static function run_xsl_transform($info) {
    $xsl = new DOMDocument();
    $xsl->load($info['xsl']);

    $input = new DOMDocument();
    $input->loadXML($info['input']);

    $processor = new XSLTProcessor();
    $processor->importStylesheet($xsl);

    if (isset($info['php_functions'])) {
      $processor->registerPHPFunctions($info['php_functions']);
    }

    // XXX: Suppressing warnings regarding unregistered prefixes.
    return $processor->transformToXML($input);
  }

  /**
   * Function to get the mods.
   */
  protected function get_mods() {
    if (!isset($this['MODS'])) {
      $dir = drupal_get_path('module', 'islandora_batch');
      // TODO: Create the MODS datastream on this object.
      $mods_datastream = $this->constructDatastream('MODS', 'X');
      $mods_datastream->mimetype = 'text/xml';
      $mods_datastream->label = 'MODS Record';

      // If we have an XML stream which is:
      if (isset($this->object_info['xml'])) {
        $xml = file_get_contents($this->object_info['xml']->uri);

        if ($xml) {
          $s_xml = new SimpleXMLElement($xml);
          // MODS, set.
          if ($s_xml->getName() == 'mods') {
            // XXX: Should probably create a resource entry.
            $mods_datastream->setContentFromFile($this->object_info['xml']->uri, FALSE);
          }
          // MARCXML, transform to MODS and set.
          elseif ($s_xml->getName() == 'record') {
            $mods_datastream->content = static::run_xsl_transform(array(
              'input' => $xml,
              'xsl' => $dir . '/transforms/MARC21slim2MODS3-4.xsl',
            ));
          }
          // DC, transform to MODS and set.
          elseif ($s_xml->getName() == 'dc') {
            $mods_datastream->content = static::run_xsl_transform(array(
              'input' => $xml,
              'xsl' => $dir . '/transforms/simpleDC2MODS.xsl',
            ));
          }
          // Unrecognized format...
          else {
            // Do nothing?
          }
        }
      }

      // We use some Java code from LoC to transform binary MARC to MODS.
      if (!isset($mods_datastream->content) && isset($this->object_info['mrc'])) {
        // Generate some MODS from binary MARC.
        // Get the java executable.
        $java = variable_get('islandora_batch_java', 'java');
        $temp_file_marc = tempnam('/tmp', 'binmrc');
        $temp_file_mods = tempnam('/tmp', 'mods');

        // Copy the file, so we're sure it's accessible on the filesystem.
        copy($this->object_info['mrc']->uri, $temp_file_marc);
        $absolute = realpath($dir);

        // The command to run.
        $command = "$java -cp $absolute/transforms/marcxml/marcxml.jar:$absolute/transforms/marcxml/marc4j.jar gov.loc.marcxml.MARC2MODS $temp_file_marc $temp_file_mods";

        // Run the command.
        $proc = popen($command, 'r');
        pclose($proc);

        // Load the output into a DOMDocument.
        $doc = new DOMDocument();
        $doc->load($temp_file_mods);

        // Drop the temp.
        unlink($temp_file_marc);
        unlink($temp_file_mods);

        $mods_docs = $doc->getElementsByTagName('mods');
        if ($mods_docs->length > 0) {
          // Grab the first.
          $mods = new DOMDocument();
          $mods_node = $mods_docs->item(0);
          $mods_node = $mods->importNode($mods_node, TRUE);
          $mods->appendChild($mods_node);

          // TODO: Transform dates to Solr format...  Or should this be left to
          // a core hook?
          // And put the content into the datastream.
          $mods_datastream->content = $mods->saveXML();
        }
      }

      if (!isset($mods_datastream->content)) {
        // If we do not yet have any XML, create a dumb MODS with just the
        // title, and set it...
        $title = $this->base_name;

        $mods_datastream->content = <<<EOXML
<mods xmlns:mods="http://www.loc.gov/mods/v3" xmlns="http://www.loc.gov/mods/v3">
  <titleInfo>
    <title>$title</title>
  </titleInfo>
</mods>
EOXML;
      }

      $this->ingestDatastream($mods_datastream);
    }

    return $this['MODS']->content;
  }
  /**
   * Function to get dc.
   */
  protected function get_dc() {
    if (!isset($this['DC'])) {
      // TODO: Create the DC datastream on this object.
      $dc_datastream = $this->constructDatastream('DC', 'X');
      $dc_datastream->mimetype = 'text/xml';
      $dc_datastream->label = 'DC Record';

      if (isset($this->object_info['xml'])) {
        $xml = file_get_contents($this->object_info['xml']->uri);

        if ($xml) {
          $s_xml = new SimpleXMLElement($xml);
          if ($s_xml->getName() == 'dc') {
            // XXX: Should probably create a resource entry.
            $dc_datastream->setContentFromFile($this->object_info['xml']->uri, FALSE);
          }
        }
      }

      // Get the DC by transforming from MODS.
      if (!isset($dc_datastream->content)) {
        // XXX: Might want to make this use a file, instead of staging the DC
        // in the database table (inside the object we serialize).
        $new_dc = static::run_xsl_transform(array(
          'xsl' => drupal_get_path('module', 'islandora_batch') . '/transforms/mods_to_dc.xsl',
          'input' => $this->get_mods(),
        ));
        if ($new_dc) {
          $dc_datastream->content = $new_dc;
        }
      }

      $this->ingestDatastream($dc_datastream);
    }

    return $this['DC']->content;
  }

  /**
   * Add collection and content model relationships.
   */
  public function add_relationships() {
    $collection = $this->preprocessor_parameters['parent'];
    $predicate = $this->preprocessor_parameters['parent_relationship_pred'];
    $uri = $this->preprocessor_parameters['parent_relationship_uri'];

    $this->relationships->add($uri, $predicate, $collection);
    $this->add_content_model_relationships();
  }

  /**
   * Add the content model relationship(s).
   */
  protected function add_content_model_relationships() {
    $this->models = $this->preprocessor_parameters['content_models'];
  }
}
// @codingStandardsIgnoreEnd
