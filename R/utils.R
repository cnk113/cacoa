#' @import Matrix
#' @import ggplot2
NULL

#' @useDynLib cacoa
NULL

.onUnload <- function (libpath) {
  library.dynam.unload("cacoa", libpath)
}

checkPackageInstalled <- function(pkgs, details='to run this function', install.help=NULL, bioc=FALSE, cran=FALSE) {
  pkgs <- pkgs[!sapply(pkgs, requireNamespace, quietly=TRUE)]
  if (length(pkgs) == 0)
    return()

  if (length(pkgs) > 1) {
    pkgs <- paste0("c('", paste0(pkgs, collapse="', '"), "')")
    error.text <- paste("Packages", pkgs, "must be installed", details)
  } else {
    pkgs <- paste0("'", pkgs, "'")
    error.text <- paste(pkgs, "package must be installed", details)
  }

  if (!is.null(install.help)) {
    error.text <- paste0(error.text, ". Please, run `", install.help, "` to do it.")
  } else if (bioc) {
    error.text <- paste0(error.text, ". Please, run `BiocManager::install(", pkgs, ")", "` to do it.")
  } else if (cran) {
    error.text <- paste0(error.text, ". Please, run `install.packages(", pkgs, ")", "` to do it.")
  }

  stop(error.text)
}
