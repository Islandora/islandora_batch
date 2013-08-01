<?php

/**
 * @file
 * The admin form.
 */

/**
 * Admin form for the batch.
 */
function islandora_batch_admin_form($form, $form_state) {
  $form['islandora_batch_java'] = array(
    '#type' => 'textfield',
    '#title' => t('Java Executable'),
    '#description' => t('The Java executable to use to run MARC to MODS transformation.'),
    '#default_value' => variable_get('islandora_batch_java', 'java'),
  );

  return system_settings_form($form);
}
