
# c.factor is now deprecated.
# It wasn't clean to do this, even internally in data.table.
# Since [.factor (also deprecated) used to drop unused levels, c.factor built them up again.
# We now drop factor levels in .SD, work with integers, and add levels back afterwards.

#c.factor = function(...)
#{
#    args <- list(...)
#    for (i in seq(along=args)) if (!is.factor(args[[i]])) args[[i]] = as.factor(args[[i]])
#    # The first must be factor otherwise we wouldn't be inside c.factor, its checked anyway in the line above.
#    newlevels = sort(unique(unlist(lapply(args,levels))))
#    ans = unlist(lapply(args, function(x) {
#        m = match(levels(x), newlevels)
#        m[as.integer(x)]
#    }))
#    levels(ans) = newlevels
#    class(ans) = "factor"
#    ans
#}
