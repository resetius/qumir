let browserIo = null;

function usesImport(imports, prefix) {
  return imports.some(item => (
    item &&
    item.module === 'env' &&
    typeof item.name === 'string' &&
    item.name.startsWith(prefix)
  ));
}

async function optionalImport(path, enabled) {
  if (!enabled) return null;
  try {
    return await import(path);
  } catch {
    return null;
  }
}

export async function loadRuntime(bytes) {
  const module = await WebAssembly.compile(bytes);
  const imports = WebAssembly.Module.imports(module);

  const [
    mathEnv,
    ioEnv,
    resultEnv,
    stringEnv,
    arrayEnv,
    complexEnv,
    futureEnv,
    ioWrapper,
    turtleModule,
    robotModule,
    drawerModule,
    painterModule,
    colorsModule,
    keyboardModule,
  ] = await Promise.all([
    import('./math.js'),
    import('./io.js'),
    import('./result.js'),
    import('./string.js'),
    import('./array.js'),
    import('./complex.js'),
    import('./future.js'),
    import('../io_wrapper.js'),
    optionalImport('./turtle.js', usesImport(imports, 'turtle_')),
    optionalImport('./robot.js', usesImport(imports, 'robot_')),
    optionalImport('./drawer.js', usesImport(imports, 'drawer_')),
    optionalImport('./painter.js', usesImport(imports, 'painter_')),
    optionalImport('./colors.js', usesImport(imports, 'color_')),
    optionalImport('./keyboard.js', usesImport(imports, 'keyboard_')),
  ]);

  if (!browserIo) {
    browserIo = ioWrapper.bindBrowserIO(ioEnv);
  }

  const env = {
    ...mathEnv,
    ...ioEnv,
    ...stringEnv,
    ...arrayEnv,
    ...complexEnv,
    ...futureEnv,
    ...(turtleModule || {}),
    ...(robotModule || {}),
    ...(drawerModule || {}),
    ...(painterModule || {}),
    ...(colorsModule || {}),
    ...(keyboardModule || {}),
  };
  const instance = await WebAssembly.instantiate(module, { env });

  return {
    module,
    instance,
    ioEnv,
    resultEnv,
    stringEnv,
    arrayEnv,
    complexEnv,
    futureEnv,
    turtleModule,
    robotModule,
    drawerModule,
    painterModule,
    colorsModule,
    keyboardModule,
    stdinStream: browserIo.inputStream,
  };
}
