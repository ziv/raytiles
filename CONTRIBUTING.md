# How to contribute

I'm really glad you're reading this, because we need volunteer developers to help this project come to fruition.

### Did you find a bug?

- Ensure the bug was not already reported by searching on GitHub under [Issues](https://github.com/ziv/raytiles/issues).
- If you're unable to find an open issue addressing the
  problem, [open a new one](https://github.com/ziv/raytiles/issues/new). Be sure to include a title and clear
  description, as much relevant information as possible, and a code sample or an executable test case demonstrating the
  expected behavior that is not occurring.

### Did you write a patch that fixes a bug?

- Open a new GitHub pull request with the patch.
- Ensure the PR description clearly describes the problem and solution. Include the relevant issue number if applicable.

### Do you have a new idea? A feature request?

Create a new issue and describe the feature you would like to see implemented. Be sure to include a clear description of
the
problem you're trying to solve, and how your proposed feature would solve it. If you have any relevant code samples or
mockups, please include them as well.

### Development Environment

To set up the development environment, you will need to have C++ compiler supporting C++23, and CMake installed.

To work with Mapbox maps, you will need to create an account on Mapbox and obtain an access token. You can sign up for a
free account at [Mapbox](https://www.mapbox.com/).

Fetch the code and set up the build environment:

```shell
git clone git@github.com:ziv/raytiles.git
cd raytiles
cmake -B cmake-build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
```

Build the project:

```shell
cmake --build cmake-build-debug
```




