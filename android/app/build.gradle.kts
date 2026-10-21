plugins {
  id("com.android.application")
  id("org.jetbrains.kotlin.android")
}

android {
  namespace = "org.retrocomputer.multiboard"
  base.archivesName = "retrocomputer"
  compileSdk = 35
  ndkVersion = "30.0.16248370"

  defaultConfig {
    applicationId = "org.retrocomputer.multiboard"
    minSdk = 28
    targetSdk = 35
    versionCode = 1
    versionName = "android-dev"
    // Both target devices are arm64 (the HiBreak Pro's MT6877, the Boox).
    ndk { abiFilters += "arm64-v8a" }
    externalNativeBuild {
      cmake {
        arguments += listOf("-DANDROID_STL=c++_static")
        cppFlags += "-std=c++17"
      }
    }
  }

  externalNativeBuild {
    cmake {
      path = file("src/main/cpp/CMakeLists.txt")
      version = "3.22.1+"
    }
  }

  buildTypes {
    release {
      isMinifyEnabled = false
    }
  }

  compileOptions {
    sourceCompatibility = JavaVersion.VERSION_17
    targetCompatibility = JavaVersion.VERSION_17
  }
  kotlinOptions {
    jvmTarget = "17"
  }
}
