// SPDX-FileCopyrightText: 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.model

import android.net.Uri
import android.widget.Toast
import androidx.documentfile.provider.DocumentFile
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import androidx.preference.PreferenceManager
import java.util.Locale
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.serialization.json.Json
import org.yuzu.yuzu_emu.NativeLibrary
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.YuzuApplication
import org.yuzu.yuzu_emu.utils.GameHelper
import org.yuzu.yuzu_emu.utils.NativeConfig
import java.util.concurrent.atomic.AtomicBoolean

class GamesViewModel : ViewModel() {
    val games: StateFlow<List<Game>> get() = _games
    private val _games = MutableStateFlow(emptyList<Game>())

    val isReloading: StateFlow<Boolean> get() = _isReloading
    private val _isReloading = MutableStateFlow(false)

    private val reloading = AtomicBoolean(false)

    val shouldSwapData: StateFlow<Boolean> get() = _shouldSwapData
    private val _shouldSwapData = MutableStateFlow(false)

    val shouldScrollToTop: StateFlow<Boolean> get() = _shouldScrollToTop
    private val _shouldScrollToTop = MutableStateFlow(false)

    val searchFocused: StateFlow<Boolean> get() = _searchFocused
    private val _searchFocused = MutableStateFlow(false)

    val shouldScrollAfterReload: StateFlow<Boolean> get() = _shouldScrollAfterReload
    private val _shouldScrollAfterReload = MutableStateFlow(false)

    private val _folders = MutableStateFlow(mutableListOf<GameDir>())
    val folders = _folders.asStateFlow()

    private val _filteredGames = MutableStateFlow<List<Game>>(emptyList())

    var lastScrollPosition: Int = 0

    init {
        // Ensure keys are loaded so that ROM metadata can be decrypted.
        NativeLibrary.reloadKeys()

        getGameDirs()
        reloadGames(directoriesChanged = false, firstStartup = true)
    }

    fun setGames(games: List<Game>) {
        val sortedList = games.sortedWith(
            compareBy(
                { it.title.lowercase(Locale.getDefault()) },
                { it.path }
            )
        )

        _games.value = sortedList
    }

    fun setShouldSwapData(shouldSwap: Boolean) {
        _shouldSwapData.value = shouldSwap
    }

    fun setShouldScrollToTop(shouldScroll: Boolean) {
        _shouldScrollToTop.value = shouldScroll
    }

    fun setShouldScrollAfterReload(shouldScroll: Boolean) {
        _shouldScrollAfterReload.value = shouldScroll
    }

    fun setSearchFocused(searchFocused: Boolean) {
        _searchFocused.value = searchFocused
    }

    fun setFilteredGames(games: List<Game>) {
        _filteredGames.value = games
    }

    fun reloadGames(directoriesChanged: Boolean, firstStartup: Boolean = false) {
        if (reloading.get()) {
            return
        }
        reloading.set(true)
        _isReloading.value = true

        viewModelScope.launch {
            withContext(Dispatchers.IO) {
                if (firstStartup) {
                    // Retrieve list of cached games
                    val storedGames =
                        PreferenceManager.getDefaultSharedPreferences(YuzuApplication.appContext)
                            .getStringSet(GameHelper.KEY_GAMES, emptySet())
                    if (storedGames!!.isNotEmpty()) {
                        val deserializedGames = mutableSetOf<Game>()
                        storedGames.forEach {
                            val game: Game
                            try {
                                game = Json.decodeFromString(it)
                            } catch (e: Exception) {
                                // We don't care about any errors related to parsing the game cache
                                return@forEach
                            }

                            val gameExists =
                                DocumentFile.fromSingleUri(
                                    YuzuApplication.appContext,
                                    Uri.parse(game.path)
                                )?.exists()
                            if (gameExists == true) {
                                deserializedGames.add(game)
                            }
                        }
                        setGames(deserializedGames.toList())
                    }
                }

                setGames(GameHelper.getGames())
                reloading.set(false)
                _isReloading.value = false
                _shouldScrollAfterReload.value = true

                if (directoriesChanged) {
                    setShouldSwapData(true)
                }
            }
        }
    }

    fun addFolder(gameDir: GameDir,  savedFromGameFragment: Boolean) =
        viewModelScope.launch {
            withContext(Dispatchers.IO) {
                NativeConfig.addGameDir(gameDir)
                getGameDirs(true)
            }

            if (savedFromGameFragment) {
                NativeConfig.saveGlobalConfig()
                Toast.makeText(
                    YuzuApplication.appContext,
                    R.string.add_directory_success,
                    Toast.LENGTH_SHORT
                ).show()
            }
        }

    fun removeFolder(gameDir: GameDir) =
        viewModelScope.launch {
            withContext(Dispatchers.IO) {
                val gameDirs = _folders.value.toMutableList()
                val removedDirIndex = gameDirs.indexOf(gameDir)
                if (removedDirIndex != -1) {
                    gameDirs.removeAt(removedDirIndex)
                    NativeConfig.setGameDirs(gameDirs.toTypedArray())
                    getGameDirs()
                }
            }
        }

    fun updateGameDirs() =
        viewModelScope.launch {
            withContext(Dispatchers.IO) {
                NativeConfig.setGameDirs(_folders.value.toTypedArray())
                getGameDirs()
            }
        }

    fun onOpenGameFoldersFragment() =
        viewModelScope.launch {
            withContext(Dispatchers.IO) {
                getGameDirs()
            }
        }

    fun onCloseGameFoldersFragment() {
        NativeConfig.saveGlobalConfig()
        viewModelScope.launch {
            withContext(Dispatchers.IO) {
                getGameDirs(true)
            }
        }
    }

    private fun getGameDirs(reloadList: Boolean = false) {
        val gameDirs = NativeConfig.getGameDirs()
        _folders.value = gameDirs.toMutableList()
        if (reloadList) {
            reloadGames(true)
        }
    }
}
