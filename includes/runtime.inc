<?php
/**
 * @file
 *  Contains functions that only are necessary when a service call is made.
 *  This has broken out so that this code isn't loaded for every page load.
 */

/**
 * Performs access checks and executes a services controller.
 * This method is called by server implementations.
 *
 * @param array $controller
 *  An array containing information about the controller
 * @param array $args
 *  The arguments that should be passed to the controller.
 * @param array $options
 *  Options for the execution. Use 'skip_authentication'=>TRUE to skip the
 *  services-specific authentication checks. Access checks will always be
 *  made.
 */
function services_controller_execute($controller, $args = array(), $options=array()) {
  // Check for missing arguments.
  $hash_parameters = array();
  foreach ($controller['args'] as $key => $arg) {
    if (!$arg['optional']) {
      if (!isset($args[$key]) && !is_array($args[$key]) && !is_bool($args[$key])) {
        return services_error(t('Missing required argument !name.', array(
          '!name' => $key,
        )), 406);
      }
    }
  }

  // Check authentication
  if (!isset($options['skip_authentication']) || !$options['skip_authentication']) {
    $endpoint_name = services_get_server_info('endpoint');
    $endpoint = services_get_endpoint($endpoint_name);

    if (!empty($endpoint['authentication'])) { // Only check authentication if there's a configured authentication module
      if ($auth_error = services_auth_invoke($endpoint['authentication'], 'authenticate_call', $controller, $args)) {
        return services_error($auth_error, 401);
      }
    }
  }

  // Load the proper file
  if ($file = $controller['file']) {
    module_load_include($file['file'], $file['module'], (isset($file['name'])?$file['name']:NULL));
  }

  // Construct access arguments array
  if (isset($controller['access arguments'])) {
    $access_arguments = $controller['access arguments'];
    if (isset($controller['access arguments append']) && $controller['access arguments append']) {
      $access_arguments[] = $args;
    }
  }
  else {
    // Just use the arguments array if no access arguments have been specified
    $access_arguments = $args;
  }

  // Call default or custom access callback
  if (call_user_func_array($controller['access callback'], $access_arguments) != TRUE) {
    global $user;
    return services_error(t('Access denied for user !uid "@user"', array(
      '!uid' => $user->uid,
      '@user' => $user->name,
    )), 401);
  }

  // Change working directory to drupal root to call drupal function,
  // then change it back to server module root to handle return.
  $server_root = getcwd();
  $drupal_path = services_get_server_info('drupal_path');
  if ($drupal_path) {
    chdir($drupal_path);
  }

  // Execute the controller callback
  $result = call_user_func_array($controller['callback'], $args);

  if ($server_info) {
    chdir($server_root);
  }

  return $result;
}

/**
 * Gets information about a authentication module.
 * If a property name is passed the value of the property will be returned,
 * otherwise the whole information array will be returned.
 *
 * @param string $property
 *  Optional. The name of a single property to get. Defaults to null.
 * @param string $module
 *  Optional. The module to get info for. Defaults to the current authentication module.
 * @return mixed
 *  The information array or property value, or FALSE if the information or property wasn't found
 */
function services_auth_info($property = NULL, $module = NULL) {
  static $info = array();
  // Default the module param to the current auth module
  // TODO: There is no longer a default auth module.
  $module = $module ? $module : variable_get('services_auth_module', '');

  if (!isset($info[$module])) {
    if (!empty($module) && module_exists($module) && is_callable($module . '_authentication_info')) {
      $info[$module] = call_user_func($module . '_authentication_info');
    }
    else {
      $info[$module] = FALSE;
    }
  }

  // If a property was requested it should be returned
  if ($property && $info[$module]) {
    return isset($info[$module][$property]) ? $info[$module][$property] : FALSE;
  }

  // Return the info array
  return $info[$module];
}

/**
 * Invokes a authentication module callback.
 *
 * @param string $module
 *  The authentication module to invoke the callback for.
 * @param string $method
 *  The callback to invoke.
 * @param string $arg1
 *  Optional. First argument to pass to the callback.
 * @param string $arg2
 *  Optional. Second argument to pass to the callback.
 * @param string $arg3
 *  Optional. Third argument to pass to the callback.
 * @return mixed
 */
function services_auth_invoke($module, $method, &$arg1 = NULL, &$arg2 = NULL, &$arg3 = NULL) {
  // Get information about the auth module
  $func = services_auth_info($method, $module);
  if ($func) {
    if ($file = services_auth_info('#file', $module)) {
      require_once(drupal_get_path('module', $module) . '/' . $file);
    }

    if (is_callable($func)) {
      $args = func_get_args();
      // Replace module and method name and arg1 with reference to $arg1 and $arg2.
      array_splice($args, 0, 4, array(&$arg1, &$arg2, &$arg3));
      return call_user_func_array($func, $args);
    }
  }
  else{
    return TRUE;
  }
}

/**
 * Formats a resource uri using the formatter registered through
 * services_set_server_info().
 *
 * @param array $path
 *  An array of strings containing the component parts of the path to the resource.
 * @return string
 *  Returns the formatted resource uri, or NULL if no formatter has been registered.
 */
function services_resource_uri($path) {
  $formatter = services_get_server_info('resource_uri_formatter');
  if ($formatter) {
    return call_user_func($formatter, $path);
  }
  return NULL;
}

/**
 * Sets a server info value
 *
 * @param string $key
 *  The key of the server info value.
 * @param mixed $value
 *  The value.
 * @return void
 */
function services_set_server_info($key, $value) {
  $info = services_server_info_object();
  $info->$key = $value;
}

/**
 * Sets multiple server info values from a associative array.
 *
 * @param array $values
 *  An associative array containing server info values.
 * @return void
 */
function services_set_server_info_from_array($values) {
  $info = services_server_info_object();
  foreach ($values as $key => $value) {
    $info->$key = $value;
  }
}

/**
 * Gets a server info value.
 *
 * @param string $key
 *  The key for the server info value.
 * @param mixed $default
 *  The default value to return if the value isn't defined.
 * @return mixed
 *  The server info value.
 */
function services_get_server_info($key, $default = NULL) {
  $info = services_server_info_object();
  $value = $default;
  if (isset($info->$key)) {
    $value = $info->$key;
  }
  return $value;
}

/**
 * Gets the server info object.
 *
 * @param bool $reset
 *  Pass TRUE if the server info object should be reset.
 * @return object
 *  Returns the server info object.
 */
function services_server_info_object($reset = FALSE) {
  static $info;
  if (!$info) {
    $info = new stdClass();
  }
  return $info;
}

/**
 * Prepare an error message for returning to the server.
 *
 * @param string $message
 *  Error message.
 * @param int $code
 *  Optional. Error code. This often maps to the HTTP status codes. Defaults
 *  to 0.
 * @param Exception $exception
 *  Optional. The exception, if any, that was thrown when the error occured.
 * @return mixed
 */
function services_error($message, $code = 0, $exception = NULL) {
  $server_module = services_get_server_info('module');

  // Look for custom error handling function.
  // Should be defined in all server modules.
  if ($server_module && function_exists($server_module . '_server_error')) {
    $message = call_user_func($server_module . '_server_error', $message, $code, $exception);
  }

  return $message;
}

/**
 * Make any changes we might want to make to node.
 */
function services_node_load($node, $fields = array()) {
  if (!isset($node->nid)) {
    return NULL;
  }

  // Loop through and get only requested fields
  if (count($fields) > 0) {
    foreach ($fields as $field) {
      $val->{$field} = $node->{$field};
    }
  }
  else {
    $val = $node;
  }

  return $val;
}

/**
 * Backup current session data and import user session.
 */
function services_session_load($sessid) {
  global $user;

  // If user's session is already loaded, just return current user's data
  if ($user->sid == $sessid) {
    return $user;
  }

  // Make backup of current user and session data
  $backup = $user;
  $backup->session = session_encode();

  // Empty current session data
  $_SESSION = array();

  // Some client/servers, like XMLRPC, do not handle cookies, so imitate it to make sess_read() function try to look for user,
  // instead of just loading anonymous user :).
  if (!isset($_COOKIE[session_name()])) $_COOKIE[session_name()] = $sessid;

  // Load session data
  session_id($sessid);
  sess_read($sessid);

  // Check if it really loaded user and, for additional security, if user was logged from the same IP. If not, then revert automatically.
  if ($user->sid != $sessid) {
    services_session_unload($backup);
    return NULL;
  }

  // Prevent saving of this impersonation in case of unexpected failure.
  session_save_session(FALSE);

  return $backup;
}

/**
 * Revert to previously backuped session.
 */
function services_session_unload($backup) {
  global $user;

  // No point in reverting if it's the same user's data
  if ($user->sid == $backup->sid) {
    return;
  }

  // Some client/servers, like XMLRPC, do not handle cookies, so imitate it to make sess_read() function try to look for user,
  // instead of just loading anonymous user :).
  if (!isset($_COOKIE[session_name()])) $_COOKIE[session_name()] = $backup->sessid;

  // Save current session data
  sess_write($user->sid, session_encode());

  // Empty current session data
  $_SESSION = array();

  // Revert to previous user and session data
  $user = $backup;
  session_id($backup->sessid);
  session_decode($user->session);

  session_save_session(TRUE);
}