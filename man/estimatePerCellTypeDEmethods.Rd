% Generated by roxygen2: do not edit by hand
% Please edit documentation in R/de_function.R
\name{estimatePerCellTypeDEmethods}
\alias{estimatePerCellTypeDEmethods}
\title{Differential expression using different methods (deseq2, edgeR, wilcoxon, ttest) with various covariates}
\usage{
estimatePerCellTypeDEmethods(
  raw.mats,
  cell.groups = NULL,
  s.groups = NULL,
  ref.level = NULL,
  target.level = NULL,
  common.genes = FALSE,
  cooks.cutoff = FALSE,
  min.cell.count = 10,
  max.cell.count = Inf,
  independent.filtering = TRUE,
  n.cores = 4,
  cluster.sep.chr = "<!!>",
  return.matrix = TRUE,
  verbose = TRUE,
  test = "Wald",
  meta.info = NULL,
  gene.filter = NULL
)
}
\arguments{
\item{raw.mats}{list of counts matrices; column for gene and row for cell}

\item{cell.groups}{factor specifying cell types (default=NULL)}

\item{ref.level}{Reference level in 'sample.groups', e.g., ctrl, healthy, wt (default=NULL)}

\item{common.genes}{Only investigate common genes across cell groups (default=F)}

\item{cooks.cutoff}{cooksCutoff for DESeq2 (default=F)}

\item{min.cell.count}{(default=10)}

\item{independent.filtering}{independentFiltering for DESeq2 (default=F)}

\item{n.cores}{Number of cores (default=1)}

\item{cluster.sep.chr}{character string of length 1 specifying a delimiter to separate cluster and app names (default="<!!>")}

\item{return.matrix}{Return merged matrix of results (default=T)}

\item{test}{DE method: deseq2, edgeR, wilcoxon, ttest}

\item{meta.info}{dataframe with possible covariates; for example, sex or age}

\item{sample.groups}{a list of two character vector specifying the app groups to compare (default=NULL)}

\item{covariates}{list of covariates to include; for example, cdr, sex or age}
}
\description{
Differential expression using different methods (deseq2, edgeR, wilcoxon, ttest) with various covariates
}
